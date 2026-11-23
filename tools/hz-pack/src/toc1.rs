//! TOC1 boot package, as produced by `dragonsecboot -pack boot_package.cfg`.
//!
//! Layout (all integers little-endian):
//!
//! * 64-byte header: name `sunxi-package`, magic `0x89119800`, checksum,
//!   serial, status, item count, valid length, versions, reserved, `MIE;`.
//! * one 368-byte entry per item: 64-byte name, data offset, data length,
//!   encrypt (0), type (3), run address (0), index (0), reserved, `IIE;`.
//! * item data: the first item starts at the end of the entry table rounded up
//!   to 1 KiB; each further item at the previous item's end rounded up to 1 KiB.
//! * total ("valid") length: the last item's end rounded up to 16 KiB; zero
//!   padding throughout. The checksum covers the whole package.
//!
//! `dragonsecboot` derives a run address for `u-boot` items; they are refused.

use crate::checksum::{self, put_u32};
use std::fs;
use std::path::{Path, PathBuf};

const HEADER_SIZE: usize = 64;
const ENTRY_SIZE: usize = 368;
const NAME_SIZE: usize = 64;
const MAGIC: u32 = 0x8911_9800;
const ITEM_TYPE: u32 = 3;
const ITEM_ALIGN: usize = 1024;
const TOTAL_ALIGN: usize = 16 * 1024;

pub struct Item {
    pub name: String,
    pub data: Vec<u8>,
}

fn align(value: usize, to: usize) -> usize {
    value.div_ceil(to) * to
}

/// Parse the `item=NAME, FILE` lines of a boot_package.cfg. `;` starts a
/// comment and CR characters are ignored. Relative file names are resolved
/// against `dir`.
pub fn parse_config(text: &str, dir: &Path) -> Result<Vec<(String, PathBuf)>, String> {
    let mut items = Vec::new();
    for raw in text.lines() {
        let line = raw.split(';').next().unwrap_or("").replace('\r', "");
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        if key.trim() != "item" {
            continue;
        }
        let (name, file) = value
            .split_once(',')
            .ok_or_else(|| format!("invalid item line: {}", raw.trim_end()))?;
        let name = name.trim();
        let file = file.trim().trim_end_matches(',').trim();
        if name.is_empty() || name.len() >= NAME_SIZE || file.is_empty() {
            return Err(format!("invalid item line: {}", raw.trim_end()));
        }
        if name == "u-boot" {
            return Err("u-boot items are not supported".into());
        }
        let path = Path::new(file);
        items.push((
            name.to_string(),
            if path.is_absolute() {
                path.to_path_buf()
            } else {
                dir.join(path)
            },
        ));
    }
    if items.is_empty() {
        return Err("no items in boot package config".into());
    }
    Ok(items)
}

/// Build the TOC1 package for `items`.
pub fn pack(items: &[Item]) -> Result<Vec<u8>, String> {
    let mut offsets = Vec::with_capacity(items.len());
    let mut offset = align(HEADER_SIZE + ENTRY_SIZE * items.len(), ITEM_ALIGN);
    let mut end = offset;
    for item in items {
        offsets.push(offset);
        end = offset + item.data.len();
        offset = align(end, ITEM_ALIGN);
    }
    let total = align(end, TOTAL_ALIGN);
    let mut buf = vec![0u8; total];

    buf[..13].copy_from_slice(b"sunxi-package");
    put_u32(&mut buf, 16, MAGIC);
    put_u32(
        &mut buf,
        32,
        u32::try_from(items.len()).map_err(|_| "too many items")?,
    );
    put_u32(
        &mut buf,
        36,
        u32::try_from(total).map_err(|_| "package too large")?,
    );
    buf[60..64].copy_from_slice(b"MIE;");

    for (i, (item, &data_offset)) in items.iter().zip(&offsets).enumerate() {
        let entry = HEADER_SIZE + ENTRY_SIZE * i;
        buf[entry..entry + item.name.len()].copy_from_slice(item.name.as_bytes());
        put_u32(&mut buf, entry + 64, data_offset as u32);
        put_u32(&mut buf, entry + 68, item.data.len() as u32);
        put_u32(&mut buf, entry + 76, ITEM_TYPE);
        buf[entry + 364..entry + 368].copy_from_slice(b"IIE;");
        buf[data_offset..data_offset + item.data.len()].copy_from_slice(&item.data);
    }
    checksum::write(&mut buf, 20)?;
    Ok(buf)
}

pub fn pack_file(config: &Path, output: &Path) -> Result<(), String> {
    let text = fs::read_to_string(config).map_err(|e| format!("{}: {e}", config.display()))?;
    let dir = config.parent().unwrap_or(Path::new("."));
    let mut items = Vec::new();
    for (name, path) in parse_config(&text, dir)? {
        let data = fs::read(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        items.push(Item { name, data });
    }
    let package = pack(&items)?;
    let tmp = output.with_extension("tmp");
    fs::write(&tmp, package).map_err(|e| format!("{}: {e}", tmp.display()))?;
    fs::rename(&tmp, output).map_err(|e| format!("{}: {e}", output.display()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::checksum::get_u32;

    fn item(name: &str, len: usize) -> Item {
        Item {
            name: name.into(),
            data: vec![0xa5; len],
        }
    }

    #[test]
    fn single_item_layout() {
        let buf = pack(&[item("melis-lzma", 5000)]).unwrap();
        assert_eq!(buf.len(), 16384);
        assert_eq!(&buf[..13], b"sunxi-package");
        assert_eq!(get_u32(&buf, 16), MAGIC);
        assert_eq!(get_u32(&buf, 32), 1);
        assert_eq!(get_u32(&buf, 36), 16384);
        assert_eq!(&buf[64..74], b"melis-lzma");
        assert_eq!(get_u32(&buf, 128), 1024);
        assert_eq!(get_u32(&buf, 132), 5000);
        assert_eq!(get_u32(&buf, 140), ITEM_TYPE);
        assert_eq!(&buf[428..432], b"IIE;");
        assert_eq!(buf[1024], 0xa5);
        assert_eq!(buf[1024 + 5000], 0);
    }

    #[test]
    fn items_are_aligned_to_1k_and_total_to_16k() {
        let buf = pack(&[item("a", 5000), item("b", 1024), item("c", 20000)]).unwrap();
        let entry = |i: usize| HEADER_SIZE + ENTRY_SIZE * i;
        assert_eq!(get_u32(&buf, entry(0) + 64), 2048);
        assert_eq!(get_u32(&buf, entry(1) + 64), 7168);
        assert_eq!(get_u32(&buf, entry(2) + 64), 8192);
        assert_eq!(buf.len(), 32768);
    }

    #[test]
    fn checksum_is_consistent() {
        let mut buf = pack(&[item("melis-lzma", 3)]).unwrap();
        let stored = get_u32(&buf, 20);
        checksum::write(&mut buf, 20).unwrap();
        assert_eq!(get_u32(&buf, 20), stored);
    }

    #[test]
    fn config_parsing() {
        let cfg = "[package]\r\n;item=Item_TOC_name, Item_filename,\r\nitem=melis-lzma,   epos.img.lzma\r\n";
        let items = parse_config(cfg, Path::new("/img")).unwrap();
        assert_eq!(
            items,
            vec![(
                "melis-lzma".to_string(),
                PathBuf::from("/img/epos.img.lzma")
            )]
        );
        assert!(parse_config("item=u-boot, u.bin\n", Path::new(".")).is_err());
        assert!(parse_config("[package]\n", Path::new(".")).is_err());
    }
}
