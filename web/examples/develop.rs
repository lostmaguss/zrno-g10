// SPDX-License-Identifier: GPL-2.0-or-later
//! develop <in.RAW> <out.uyvy> [daylight]: the browser tool's development of a
//! Zrno sensor backup, natively. Grey-world white balance unless `daylight`.
fn main() {
    let a: Vec<String> = std::env::args().collect();
    let mut raw = zrno_wasm::fgraw::decode(&std::fs::read(&a[1]).unwrap()).expect("not a G12 backup");
    if a.get(3).map(String::as_str) != Some("daylight") {
        raw.wb = zrno_wasm::develop::grey_world(&raw);
    }
    eprintln!("white balance {:?}", raw.wb);
    let t = std::time::Instant::now();
    let uyvy = zrno_wasm::develop::develop(&raw).unwrap();
    eprintln!("developed in {:?}", t.elapsed());
    std::fs::write(&a[2], uyvy).unwrap();
}
