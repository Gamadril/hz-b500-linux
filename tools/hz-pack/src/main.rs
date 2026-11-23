//! HZ-B500 SD image packing helpers.
//!
//! Replaces closed Allwinner host tools with byte-identical output:
//!
//! * `hz-pack toc1 CONFIG OUTPUT` — `dragonsecboot -pack` (TOC1 boot package)
//! * `hz-pack boot0-header BOOT0 SYS_CONFIG_FEX` — `script` + `update_boot0 … SDMMC_CARD`
//!
//! The host checks in `tests/boot-package` and `tests/boot0-header` compare
//! both commands with the vendor binaries.

mod boot0;
mod checksum;
mod fex;
mod toc1;

use std::path::Path;
use std::process::ExitCode;

const USAGE: &str = "\
Usage:
  hz-pack toc1 CONFIG OUTPUT
      Pack the items listed in a boot_package.cfg into a TOC1 boot package.
  hz-pack boot0-header BOOT0 SYS_CONFIG_FEX
      Write the board's sys_config.fex values into an SD Boot0 header, in place.";

fn run(args: &[String]) -> Result<(), String> {
    match args {
        [cmd, config, output] if cmd == "toc1" => {
            toc1::pack_file(Path::new(config), Path::new(output))
        }
        [cmd, boot0, fex] if cmd == "boot0-header" => {
            boot0::patch_file(Path::new(boot0), Path::new(fex))
        }
        _ => Err(USAGE.to_string()),
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match run(&args) {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("hz-pack: {message}");
            ExitCode::FAILURE
        }
    }
}
