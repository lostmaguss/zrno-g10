// SPDX-License-Identifier: GPL-2.0-or-later
//! Zrno's sensor backups, `A/FGRAW/Rnnnnnnn.RAW`: the camera writes
//! CHDK's raw buffer as it is, 3744 x 2784 12-bit samples with no header
//! (15,634,944 bytes). Every six bytes hold four samples, in the order of
//! CHDK's `get_raw_pixel` for 12 bits per pixel (core/raw.c).

use crate::develop::{Raw, RAW_H, RAW_W};

/// Size of a G12 backup in bytes.
pub const SIZE: usize = RAW_W * RAW_H * 12 / 8;

/// Unpacks a backup; None when `d` is not the size of a G12 backup.
pub fn decode(d: &[u8]) -> Option<Raw> {
    if d.len() != SIZE {
        return None;
    }
    let mut data = vec![0u16; RAW_W * RAW_H];
    for (o, b) in data.chunks_exact_mut(4).zip(d.chunks_exact(6)) {
        o[0] = (b[1] as u16) << 4 | (b[0] >> 4) as u16;
        o[1] = ((b[0] & 0x0f) as u16) << 8 | b[3] as u16;
        o[2] = (b[2] as u16) << 4 | (b[5] >> 4) as u16;
        o[3] = ((b[5] & 0x0f) as u16) << 8 | b[4] as u16;
    }
    Some(Raw { width: RAW_W, height: RAW_H, data, wb: [0; 4] })
}
