// SPDX-License-Identifier: GPL-2.0-or-later
//! Checks the sensor backup reader against CHDK's get_raw_pixel (core/raw.c,
//! 12 bits per pixel), and on a real backup when ZRNO_FGRAW points at one.
use zrno_wasm::{develop, fgraw};

const ROW_BYTES: usize = 3744 * 12 / 8;

/// CHDK's get_raw_pixel for CAM_SENSOR_BITS_PER_PIXEL 12, transcribed.
fn chdk_pixel(raw: &[u8], x: usize, y: usize) -> u16 {
    let a = &raw[y * ROW_BYTES + (x / 4) * 6..];
    match x % 4 {
        0 => (a[1] as u16) << 4 | (a[0] >> 4) as u16,
        1 => ((a[0] & 0x0f) as u16) << 8 | a[3] as u16,
        2 => (a[2] as u16) << 4 | (a[5] >> 4) as u16,
        _ => ((a[5] & 0x0f) as u16) << 8 | a[4] as u16,
    }
}

#[test]
fn unpacks_like_chdk() {
    let mut bytes = vec![0u8; fgraw::SIZE];
    let mut s = 0x2545_f491_u32;
    for b in &mut bytes {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        *b = s as u8;
    }
    let raw = fgraw::decode(&bytes).unwrap();
    assert_eq!((raw.width, raw.height), (3744, 2784));
    for y in 0..2784 {
        for x in 0..3744 {
            assert_eq!(raw.data[y * 3744 + x], chdk_pixel(&bytes, x, y), "at ({x}, {y})");
        }
    }
    assert!(fgraw::decode(&bytes[1..]).is_none());
}

#[test]
fn real_backup() {
    let Ok(path) = std::env::var("ZRNO_FGRAW") else {
        eprintln!("ZRNO_FGRAW not set: skipped");
        return;
    };
    let raw = fgraw::decode(&std::fs::read(path).unwrap()).unwrap();
    // The masked border sits at the G12's black level (about 127).
    let border: Vec<f64> = (12..2772).flat_map(|y| (0..48).map(move |x| (y, x)))
        .map(|(y, x)| raw.data[y * 3744 + x] as f64).collect();
    let mean = border.iter().sum::<f64>() / border.len() as f64;
    assert!((110.0..145.0).contains(&mean), "black level {mean}");
    let wb = develop::grey_world(&raw);
    assert!(wb[0] > 0 && wb[3] > 0 && wb[1] == 1024, "{wb:?}");
    assert_eq!(develop::develop(&raw).unwrap().len(), develop::WIDTH * develop::HEIGHT * 2);
}
