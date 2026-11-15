// SPDX-License-Identifier: GPL-2.0-or-later
//! The other engines on a synthetic frame: they run, and a neutral grade's
//! table, applied as Canon's develop would, leaves the frame nearly as it was.
use zrno_wasm::engines::{self, Exposure, GboySettings, GradeSettings, RisoSettings};

const W: usize = 3648;
const H: usize = 2736;

fn frame() -> Vec<u8> {
    let mut f = vec![0u8; W * H * 2];
    for y in 0..H {
        for x in (0..W).step_by(2) {
            let i = (y * W + x) * 2;
            f[i] = ((x * 64 / W) as i32 - 32) as i8 as u8;
            f[i + 1] = (x * 255 / W) as u8;
            f[i + 2] = ((y * 64 / H) as i32 - 32) as i8 as u8;
            f[i + 3] = (y * 255 / H) as u8;
        }
    }
    f
}

fn neutral() -> GradeSettings {
    let mut v = [0i8; 40];
    v[10] = 3; // shadow amount
    v[12] = 3; // highlight amount
    GradeSettings(v)
}

#[test]
fn neutral_grade_keeps_the_frame() {
    let table = engines::grade_table(&neutral()).expect("neutral grade is valid");
    // In-gamut colours only: luma 48..208, chroma within 12.
    let mut before = frame();
    for px in before.chunks_exact_mut(4) {
        for k in [1, 3] { px[k] = 48 + (px[k] as u32 * 160 / 255) as u8; }
        for k in [0, 2] { px[k] = ((px[k] as i8 as i32) * 12 / 32) as i8 as u8; }
    }
    let mut after = before.clone();
    engines::apply_table(&mut after, &table);
    let worst = before.iter().zip(&after).enumerate()
        .map(|(i, (&a, &b))| if i % 2 == 1 { (a as i32 - b as i32).abs() } else { (a as i8 as i32 - b as i8 as i32).abs() })
        .max().unwrap();
    assert!(worst <= 4, "neutral grade moved a value by {worst}");
}

#[test]
fn grade_out_of_range_is_refused() {
    let mut g = neutral();
    g.0[0] = 9;
    assert!(engines::grade_table(&g).is_none());
}

#[test]
fn gboy_renders() {
    let mut f = frame();
    let ex = Exposure { tv96: 672, av96: 576, iso: 400, ev96: 0 };
    assert_eq!(engines::gboy(&mut f, W, H, &GboySettings([0, 1, 7, 3, 2, 1, 0]), 1234, Some(&ex)), 0);
    assert_ne!(f, frame());
}

#[test]
fn print_renders() {
    let data = std::fs::read("../card/CHDK/FILM/PRINT.BIN").expect("PRINT.BIN");
    assert!(engines::print_data_ok(&data));
    let mut f = frame();
    assert_eq!(engines::print(&mut f, W, H, &data, &RisoSettings([0, 0, 0, 2, 2, 3, 0, 0, 0]), 1234), 0);
    assert_ne!(f, frame());
}

#[test]
fn names_are_json() {
    let j = engines::names_json();
    assert!(j.starts_with("{\"palettes\":[\"Pea green\""), "{j}");
    assert!(j.contains("\"Light leak\"") && j.contains("\"Text mode\"") && j.contains("\"4 Heavy\""), "{j}");
}
