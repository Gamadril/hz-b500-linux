//! SD Boot0 header, as written by `script` + `update_boot0 BOOT0 sys_config.bin SDMMC_CARD`.
//!
//! BROM loads Boot0 into SRAM before DRAM exists. Boot0 then sets up DRAM, the
//! debug UART and the SD controller from values in its own header, and BROM
//! refuses a header whose checksum is wrong. One generic Boot0 binary therefore
//! gets the board's sys_config values written into its header at pack time.
//! Layout: `lichee/brandy-2.0/spl/include/private_boot0.h` (`boot0_file_head_t`).

use crate::checksum::{self, get_u32, put_u32};
use crate::fex::{Fex, Gpio};
use std::fs;
use std::path::Path;

// boot_file_head_t
const MAGIC: usize = 4; // "eGON.BT0"
const CHECK_SUM: usize = 12;
const LENGTH: usize = 16;
/// `pub_head_vsn` in our struct; the vendor tool's struct has
/// `a15_power_gpio` here and clears it unless `[external_power]` sets it.
const A15_POWER_GPIO: usize = 24;
/// `platform[0]`: boot medium, 1 = SD/MMC.
const PLATFORM_MEDIUM: usize = 40;
// boot0_private_head_t
const DEBUG_MODE: usize = 52;
const POWER_MODE: usize = 53;
const DRAM_PARA: usize = 56;
const DRAM_PARA_COUNT: usize = 32;
const UART_PORT: usize = 184;
const UART_CTRL: usize = 188;
const UART_CTRL_COUNT: usize = 2;
const ENABLE_JTAG: usize = 204;
const JTAG_GPIO: usize = 208;
const JTAG_GPIO_COUNT: usize = 5;
const STORAGE_GPIO: usize = 248;
/// `storage_data`: starts with `boot_sdcard_info_t` (arrays of 4 s32, one per card).
const SDCARD_INFO: usize = 504;
const GPIO_SIZE: usize = 8;
const GPIO_SLOTS_PER_CARD: usize = 8;
const CARDS: usize = 4;
const HEADER_END: usize = SDCARD_INFO + 256;

const MEDIUM_SDMMC: u8 = 1;
const DEFAULT_DEBUG_MODE: i32 = 8;

/// `boot_sdcard_info_t` fields written from `[cardN_boot_para]`, with the
/// offset of each array inside the struct. `card_ctrl_num` (0), `boot_offset`
/// (4) and `line_count` (48) are left untouched, as the vendor tool does.
const CARD_NO: usize = 8;
const CARD_FIELDS: [(&str, usize); 10] = [
    ("card_high_speed", 24),
    ("card_line", 40),
    ("sdc_2xmode", 72),
    ("sdc_ddrmode", 88),
    ("sdc_f_max", 104),
    ("sdc_ex_dly_used", 120),
    ("sdc_odly_25M", 136),
    ("sdc_sdly_25M", 152),
    ("sdc_odly_50M", 168),
    ("sdc_sdly_50M", 184),
];

/// Card pin slots in order. `d4`–`d7`, `emmc_rst` and `ds` are written only for
/// 8-bit cards; slots 8–11 of card N overlap card N+1's first slots.
const PINS_4BIT: [&str; 6] = ["clk", "cmd", "d0", "d1", "d2", "d3"];
const PINS_8BIT_EXTRA: [&str; 6] = ["d4", "d5", "d6", "d7", "emmc_rst", "ds"];
const OPTIONAL_PINS: [&str; 2] = ["emmc_rst", "ds"];

/// sys_config features the vendor tool handles but this board does not use.
const UNSUPPORTED: [(&str, &str); 6] = [
    ("power_sply", "boot0_sys_vol"),
    ("cpus_vdd_para", "cpus_vdd_port"),
    ("twi_para", "twi_used"),
    ("external_power", "a15_pwr_en"),
    ("cpu_logical_map", "cpu0"),
    ("dram_select_para", "select_mode"),
];

fn put_gpio(buf: &mut [u8], offset: usize, gpio: &Gpio) {
    // Each field is a C char: values wrap to 8 bits, so -1 becomes 0xff.
    let bytes = [
        gpio.port, gpio.pin, gpio.mux, gpio.pull, gpio.drive, gpio.data,
    ]
    .map(|v| v as u8);
    buf[offset..offset + 6].copy_from_slice(&bytes);
}

fn card_pin(fex: &Fex, section: &str, pin: &str) -> Result<Option<Gpio>, String> {
    match fex.gpio(section, &format!("SDC_{}", pin.to_ascii_uppercase()))? {
        Some(gpio) => Ok(Some(gpio)),
        None => fex.gpio(section, &format!("sdc_{pin}")),
    }
}

/// Write the header fields of `buf` (a Boot0 image) from `fex`.
pub fn patch(buf: &mut [u8], fex: &Fex) -> Result<(), String> {
    if buf.len() < HEADER_END || &buf[MAGIC..MAGIC + 8] != b"eGON.BT0" {
        return Err("not a Boot0 image".into());
    }
    let length = get_u32(buf, LENGTH) as usize;
    if length == 0 || !length.is_multiple_of(4) || length > buf.len() {
        return Err(format!("bad Boot0 length {length}"));
    }
    for (section, key) in UNSUPPORTED {
        if fex.get(section, key).is_some() {
            return Err(format!("[{section}] {key} is not supported"));
        }
    }

    put_u32(buf, A15_POWER_GPIO, 0);
    buf[PLATFORM_MEDIUM] = MEDIUM_SDMMC;
    buf[DEBUG_MODE] = fex
        .int("platform", "debug_mode")?
        .unwrap_or(DEFAULT_DEBUG_MODE) as u8;
    if let Some(mode) = fex.int("target", "power_mode")? {
        buf[POWER_MODE] = mode as u8;
    }

    let dram = fex.section("dram_para");
    if dram.is_empty() || dram.len() > DRAM_PARA_COUNT {
        return Err(format!(
            "[dram_para] needs 1 to {DRAM_PARA_COUNT} values, found {}",
            dram.len()
        ));
    }
    for (i, (key, _)) in dram.iter().enumerate() {
        let value = fex.int("dram_para", key)?.unwrap_or_default();
        put_u32(buf, DRAM_PARA + 4 * i, value as u32);
    }

    if let Some(port) = fex.int("uart_para", "uart_debug_port")? {
        put_u32(buf, UART_PORT, port as u32);
    }
    let uart = fex.gpios("uart_para")?;
    if uart.len() > UART_CTRL_COUNT {
        return Err(format!("[uart_para] has more than {UART_CTRL_COUNT} pins"));
    }
    for (i, gpio) in uart.iter().enumerate() {
        put_gpio(buf, UART_CTRL + GPIO_SIZE * i, gpio);
    }

    if let Some(enable) = fex.int("jtag_para", "jtag_enable")? {
        put_u32(buf, ENABLE_JTAG, enable as u32);
    }
    let jtag = fex.gpios("jtag_para")?;
    if jtag.len() > JTAG_GPIO_COUNT {
        return Err(format!("[jtag_para] has more than {JTAG_GPIO_COUNT} pins"));
    }
    for (i, gpio) in jtag.iter().enumerate() {
        put_gpio(buf, JTAG_GPIO + GPIO_SIZE * i, gpio);
    }

    for card in 0..CARDS {
        let section = format!("card{card}_boot_para");
        let info = |field: usize| SDCARD_INFO + field + 4 * card;
        let Some(ctrl) = fex.int(&section, "card_ctrl")? else {
            put_u32(buf, info(CARD_NO), u32::MAX);
            continue;
        };
        put_u32(buf, info(CARD_NO), ctrl as u32);
        for (key, field) in CARD_FIELDS {
            if let Some(value) = fex.int(&section, key)? {
                put_u32(buf, info(field), value as u32);
            }
        }
        let mut pins: Vec<&str> = PINS_4BIT.to_vec();
        if fex.int(&section, "card_line")? == Some(8) {
            pins.extend(PINS_8BIT_EXTRA);
        }
        for (slot, pin) in pins.iter().enumerate() {
            let offset = STORAGE_GPIO + GPIO_SIZE * (GPIO_SLOTS_PER_CARD * card + slot);
            match card_pin(fex, &section, pin)? {
                Some(gpio) => put_gpio(buf, offset, &gpio),
                None if OPTIONAL_PINS.contains(pin) => {}
                None => return Err(format!("[{section}] has no sdc_{pin}")),
            }
        }
    }

    checksum::write(&mut buf[..length], CHECK_SUM)
}

pub fn patch_file(boot0: &Path, fex_path: &Path) -> Result<(), String> {
    let text = fs::read(fex_path).map_err(|e| format!("{}: {e}", fex_path.display()))?;
    let fex = Fex::parse(&String::from_utf8_lossy(&text));
    let mut buf = fs::read(boot0).map_err(|e| format!("{}: {e}", boot0.display()))?;
    patch(&mut buf, &fex)?;
    fs::write(boot0, buf).map_err(|e| format!("{}: {e}", boot0.display()))
}

#[cfg(test)]
mod tests {
    use super::*;

    const BOARD: &str = "\
[platform]\ndebug_mode = 8\n\
[uart_para]\nuart_debug_port = 0\nuart_debug_tx = port:PE2<6><1><default><default>\nuart_debug_rx = port:PE3<1><1><default><default>\n\
[jtag_para]\njtag_enable = 0\njtag_ms = port:PE4<7><default><default><default>\n\
[dram_para]\ndram_clk = 528\ndram_type = 2\ndram_tpr3 = -1267173226\n\
[card0_boot_para]\ncard_ctrl = 0\ncard_high_speed = 1\ncard_line = 4\n\
sdc_d1 = port:PF0<2><1><default><default>\nsdc_d0 = port:PF1<2><1><default><default>\n\
sdc_clk = port:PF2<2><1><default><default>\nsdc_cmd = port:PF3<2><1><default><default>\n\
sdc_d3 = port:PF4<2><1><default><default>\nsdc_d2 = port:PF5<2><1><default><default>\n";

    fn blank_boot0() -> Vec<u8> {
        let mut buf = vec![0u8; 4096];
        buf[MAGIC..MAGIC + 8].copy_from_slice(b"eGON.BT0");
        put_u32(&mut buf, LENGTH, 4096);
        buf
    }

    #[test]
    fn writes_board_fields() {
        let mut buf = blank_boot0();
        patch(&mut buf, &Fex::parse(BOARD)).unwrap();
        assert_eq!(buf[PLATFORM_MEDIUM], 1);
        assert_eq!(buf[DEBUG_MODE], 8);
        assert_eq!(get_u32(&buf, DRAM_PARA), 528);
        assert_eq!(get_u32(&buf, DRAM_PARA + 8) as i32, -1_267_173_226);
        assert_eq!(
            &buf[UART_CTRL..UART_CTRL + 8],
            &[5, 2, 6, 1, 0xff, 0xff, 0, 0]
        );
        assert_eq!(&buf[JTAG_GPIO..JTAG_GPIO + 6], &[5, 4, 7, 0xff, 0xff, 0xff]);
        // Slot order is fixed: clk, cmd, d0, d1, d2, d3.
        let pins: Vec<u8> = (0..6)
            .map(|s| buf[STORAGE_GPIO + GPIO_SIZE * s + 1])
            .collect();
        assert_eq!(pins, [2, 3, 1, 0, 5, 4]);
        assert_eq!(get_u32(&buf, SDCARD_INFO + CARD_NO), 0);
        assert_eq!(get_u32(&buf, SDCARD_INFO + CARD_NO + 4), u32::MAX);
        assert_eq!(get_u32(&buf, SDCARD_INFO + 40), 4);
    }

    #[test]
    fn checksum_matches_contents() {
        let mut buf = blank_boot0();
        patch(&mut buf, &Fex::parse(BOARD)).unwrap();
        let stored = get_u32(&buf, CHECK_SUM);
        checksum::write(&mut buf, CHECK_SUM).unwrap();
        assert_eq!(get_u32(&buf, CHECK_SUM), stored);
    }

    #[test]
    fn rejects_unsupported_and_incomplete_configs() {
        let mut buf = blank_boot0();
        let pmic = format!("{BOARD}[power_sply]\nboot0_sys_vol = dcdc2_vol\n");
        assert!(patch(&mut buf, &Fex::parse(&pmic)).is_err());
        let no_clk = BOARD.replace("sdc_clk", "unused");
        assert!(patch(&mut buf, &Fex::parse(&no_clk)).is_err());
        assert!(patch(&mut vec![0u8; 4096], &Fex::parse(BOARD)).is_err());
    }
}
