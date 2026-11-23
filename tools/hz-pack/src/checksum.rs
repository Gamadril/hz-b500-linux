//! The sunxi "add sum" checksum shared by Boot0 and TOC1 headers.

/// Value stored in the checksum field while the sum is computed.
pub const STAMP_VALUE: u32 = 0x5F0A_6C39;

/// Write the checksum of `buf` into the little-endian u32 at `field`.
///
/// The field is first set to [`STAMP_VALUE`], then all 32-bit little-endian
/// words of `buf` are summed with wrap-around and the sum is stored in it.
pub fn write(buf: &mut [u8], field: usize) -> Result<(), String> {
    if !buf.len().is_multiple_of(4) {
        return Err(format!(
            "checksummed length {} is not a multiple of 4",
            buf.len()
        ));
    }
    put_u32(buf, field, STAMP_VALUE);
    let sum = buf
        .as_chunks::<4>()
        .0
        .iter()
        .map(|w| u32::from_le_bytes(*w))
        .fold(0u32, u32::wrapping_add);
    put_u32(buf, field, sum);
    Ok(())
}

pub fn put_u32(buf: &mut [u8], offset: usize, value: u32) {
    buf[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

pub fn get_u32(buf: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes([
        buf[offset],
        buf[offset + 1],
        buf[offset + 2],
        buf[offset + 3],
    ])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn checksum_counts_the_stamp_in_its_own_field() {
        let mut buf = vec![0u8; 16];
        put_u32(&mut buf, 8, 1);
        write(&mut buf, 4).unwrap();
        assert_eq!(get_u32(&buf, 4), STAMP_VALUE.wrapping_add(1));
    }

    #[test]
    fn checksum_wraps_around() {
        let mut buf = vec![0xffu8; 8];
        write(&mut buf, 0).unwrap();
        assert_eq!(get_u32(&buf, 0), STAMP_VALUE.wrapping_add(0xffff_ffff));
    }

    #[test]
    fn rejects_unaligned_length() {
        assert!(write(&mut [0u8; 6], 0).is_err());
    }
}
