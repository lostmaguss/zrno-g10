// SPDX-License-Identifier: GPL-2.0-or-later
//! Compiles the camera's effect kernels (../camera/*.c, unchanged) with clang and
//! links the objects in: for wasm32 freestanding with tiny string.h and
//! stdlib.h shims (malloc and free are in lib.rs), for
//! the host as ordinary objects (used by `cargo test`).
use std::{env, path::PathBuf, process::Command};

const SOURCES: [&str; 9] = [
    "film_colour.c", "film_retro.c", "film_toy.c", "film_print.c", "film_bloom.c", "film_grain.c",
    "film_gboy.c", "film_riso.c", "film_grade.c",
];

fn main() {
    let target = env::var("TARGET").unwrap();
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    let here = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let camera = here.join("../camera");
    let wasm = target.starts_with("wasm32");
    println!("cargo:rerun-if-changed=shim/string.h");
    println!("cargo:rerun-if-changed=shim/stdlib.h");
    for name in SOURCES {
        let src = camera.join(name);
        println!("cargo:rerun-if-changed={}", src.display());
        let obj = out.join(format!("{name}.o"));
        let mut cc = Command::new(env::var("CC").unwrap_or_else(|_| "clang".into()));
        if wasm {
            cc.args(["--target=wasm32-unknown-unknown", "-ffreestanding", "-nostdlib"]);
            cc.arg("-I").arg(here.join("shim"));
        }
        // The camera build's switch. Its only effect is in film_colour.c, where
        // it leaves out the CPU LUT (the camera grades with the DIGIC's colour
        // table). The browser has no DIGIC, so film_colour.c is built without
        // it: the CPU LUT is the reference the camera's tables are fitted to.
        if name != "film_colour.c" {
            cc.arg("-DFILM_LAB_CAMERA");
        }
        cc.args(["-O2", "-std=gnu99", "-Wall", "-Wextra", "-c"])
            .arg("-I").arg(&camera).arg(&src).arg("-o").arg(&obj);
        let status = cc.status().expect("run clang");
        assert!(status.success(), "compiling {name} failed");
        println!("cargo:rustc-link-arg={}", obj.display());
    }
    for h in ["film_colour.h", "film_retro.h", "film_toy.h", "film_print.h", "film_font.h", "film_bloom.h", "film_grain.h",
              "film_gboy.h", "film_riso.h", "film_grade.h"] {
        println!("cargo:rerun-if-changed={}", camera.join(h).display());
    }
}
