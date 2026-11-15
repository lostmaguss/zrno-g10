// SPDX-License-Identifier: GPL-2.0-or-later
//! Zrno in the browser: the camera's effect kernels (C, unchanged)
//! plus a developer for Zrno's sensor backups (A/FGRAW/*.RAW), behind a
//! small C-ABI for a Web Worker.
//!
//! Calls from JavaScript, in order: `fl_alloc` a buffer and copy a file in,
//! `fl_load_raw` (or `fl_load_rgba` for the test chart), `fl_set_lut` with
//! a look's .LUT bytes, then `fl_render` for each change and read
//! `fl_width() * fl_height() * 4` RGBA bytes at `fl_output()`.

pub mod develop;
pub mod engines;
pub mod fgraw;
pub mod pipeline;

use pipeline::{Effects, Lut, PrintSettings, PRINT_TEXT};
use std::cell::RefCell;

#[derive(Default)]
struct State {
    width: usize,
    height: usize,
    base: Vec<u8>,
    work: Vec<u8>,
    rgba: Vec<u8>,
    lut: Option<Lut>,
    wb: [u16; 4],
    /// The loaded backup, kept so white balance changes re-develop it
    /// without the page sending it again.
    raw: Option<develop::Raw>,
    /// PRINT.BIN, the Print engine's inks and tiles.
    print_data: Vec<u8>,
}

thread_local! {
    static STATE: RefCell<State> = RefCell::new(State::default());
}

#[no_mangle]
pub extern "C" fn fl_alloc(n: usize) -> *mut u8 {
    let mut v = vec![0u8; n];
    let p = v.as_mut_ptr();
    core::mem::forget(v);
    p
}

/// # Safety
/// `p` and `n` must come from one `fl_alloc` call.
#[no_mangle]
pub unsafe extern "C" fn fl_free(p: *mut u8, n: usize) {
    drop(Vec::from_raw_parts(p, n, n));
}

/// malloc and free for the camera C on wasm32, where there is no libc (the
/// host build links the system's). Each block keeps its size in an 8-byte
/// header so `free` can rebuild the layout; 8-byte alignment like malloc's.
#[cfg(target_arch = "wasm32")]
mod libc_heap {
    use std::alloc::{alloc, dealloc, Layout};
    const HEAD: usize = 8;

    #[no_mangle]
    pub unsafe extern "C" fn malloc(n: usize) -> *mut u8 {
        let Some(total) = n.checked_add(HEAD) else { return core::ptr::null_mut() };
        let Ok(layout) = Layout::from_size_align(total, HEAD) else { return core::ptr::null_mut() };
        let p = alloc(layout);
        if p.is_null() {
            return p;
        }
        (p as *mut usize).write(total);
        p.add(HEAD)
    }

    #[no_mangle]
    pub unsafe extern "C" fn free(p: *mut u8) {
        if p.is_null() {
            return;
        }
        let base = p.sub(HEAD);
        let total = (base as *const usize).read();
        dealloc(base, Layout::from_size_align_unchecked(total, HEAD));
    }
}

/// White balance levels R, G, B (only their ratios matter); any zero means
/// the G12's daylight balance.
fn levels(r: u32, g: u32, b: u32) -> [u16; 4] {
    let ok = |v: u32| v > 0 && v <= u16::MAX as u32;
    if ok(r) && ok(g) && ok(b) { [r as u16, g as u16, g as u16, b as u16] } else { [0; 4] }
}

/// Develop a Zrno sensor backup (`Rnnnnnnn.RAW`) with white balance levels
/// R, G, B (any zero: daylight). Returns 0, or -1 when the bytes are not a
/// G12 backup.
/// # Safety
/// `p` must point to `n` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn fl_load_raw(p: *const u8, n: usize, r: u32, g: u32, b: u32) -> i32 {
    let Some(mut raw) = fgraw::decode(core::slice::from_raw_parts(p, n)) else { return -1 };
    raw.wb = levels(r, g, b);
    let Ok(uyvy) = develop::develop(&raw) else { return -1 };
    STATE.with(|s| {
        let mut s = s.borrow_mut();
        s.width = develop::WIDTH;
        s.height = develop::HEIGHT;
        s.wb = raw.wb;
        s.base = uyvy;
        s.rgba = vec![0u8; s.width * s.height * 4];
        s.raw = Some(raw);
    });
    0
}

/// Develop the loaded backup again with other white balance levels.
/// Returns 0, or -1 when no backup is loaded.
#[no_mangle]
pub extern "C" fn fl_set_wb(r: u32, g: u32, b: u32) -> i32 {
    STATE.with(|s| {
        let mut s = s.borrow_mut();
        let wb = levels(r, g, b);
        let Some(raw) = s.raw.as_mut() else { return -1 };
        raw.wb = wb;
        let Ok(uyvy) = develop::develop(raw) else { return -1 };
        s.wb = wb;
        s.base = uyvy;
        0
    })
}

/// Use a decoded image (RGBA, even width up to 3744). Returns 0 or -1.
/// # Safety
/// `p` must point to `width * height * 4` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn fl_load_rgba(p: *const u8, width: usize, height: usize) -> i32 {
    if width == 0 || height == 0 || width % 2 != 0 || width > 3744 {
        return -1;
    }
    let rgba = core::slice::from_raw_parts(p, width * height * 4);
    let base = pipeline::from_rgba(rgba, width, height);
    STATE.with(|s| {
        let mut s = s.borrow_mut();
        s.width = width;
        s.height = height;
        s.wb = [0; 4];
        s.raw = None;
        s.base = base;
        s.rgba = vec![0u8; width * height * 4];
    });
    0
}

/// Use a ready signed-UYVY frame (for tests comparing with the host build).
/// # Safety
/// `p` must point to `width * height * 2` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn fl_load_uyvy(p: *const u8, width: usize, height: usize) -> i32 {
    if width == 0 || height == 0 || width % 2 != 0 || width > 3744 {
        return -1;
    }
    let base = core::slice::from_raw_parts(p, width * height * 2).to_vec();
    STATE.with(|s| {
        let mut s = s.borrow_mut();
        s.width = width;
        s.height = height;
        s.wb = [0; 4];
        s.raw = None;
        s.base = base;
        s.rgba = vec![0u8; width * height * 4];
    });
    0
}
/// The graded frame of the last render, signed UYVY (`width * height * 2` bytes).
#[no_mangle]
pub extern "C" fn fl_output_uyvy() -> *const u8 {
    STATE.with(|s| s.borrow().work.as_ptr())
}
/// The developed frame before grading, signed UYVY.
#[no_mangle]
pub extern "C" fn fl_base_uyvy() -> *const u8 {
    STATE.with(|s| s.borrow().base.as_ptr())
}
#[no_mangle]
pub extern "C" fn fl_width() -> usize {
    STATE.with(|s| s.borrow().width)
}
#[no_mangle]
pub extern "C" fn fl_height() -> usize {
    STATE.with(|s| s.borrow().height)
}
#[no_mangle]
pub extern "C" fn fl_output() -> *const u8 {
    STATE.with(|s| s.borrow().rgba.as_ptr())
}
/// White balance level `i` (R, G, G, B) the loaded backup was developed with;
/// all zero for daylight.
#[no_mangle]
pub extern "C" fn fl_wb(i: usize) -> u32 {
    STATE.with(|s| s.borrow().wb.get(i).copied().unwrap_or(0) as u32)
}

/// Prepare a look from .LUT file bytes. Returns 0 or the kernel's error.
/// # Safety
/// `p` must point to `n` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn fl_set_lut(p: *const u8, n: usize) -> i32 {
    match Lut::new(core::slice::from_raw_parts(p, n)) {
        Ok(l) => {
            STATE.with(|s| s.borrow_mut().lut = Some(l));
            0
        }
        Err(e) => e,
    }
}

/// Render into the RGBA output. `mode` 0: the look with all effects;
/// 1: the look only; 2: the developed frame without a look. `text` points to
/// up to 24 bytes of stamp text (NUL-terminated or full). Returns 0, -10 with
/// nothing loaded, -11 without a look, or a kernel error.
/// # Safety
/// `text` must point to 25 readable bytes, or be null.
#[no_mangle]
pub unsafe extern "C" fn fl_render(
    mode: u32, halation: u32, bloom: u32, grain: u32, vignette: u32, leaks: u32, toy: u32,
    dust: u32, dust_dark: u32, stamp: u32, seed: u32, text: *const u8, soft: u32,
) -> i32 {
    STATE.with(|s| {
        let s = &mut *s.borrow_mut();
        if s.base.is_empty() {
            return -10;
        }
        s.work.clear();
        s.work.extend_from_slice(&s.base);
        if mode != 2 {
            let Some(lut) = s.lut.as_ref() else { return -11 };
            let fx = if mode == 0 {
                Effects { halation, bloom, grain, vignette, leaks, toy, soft, seed }
            } else {
                Effects { seed, ..Default::default() }
            };
            let mut print = PrintSettings { dust, dust_dark, stamp, text: [0; PRINT_TEXT + 1] };
            if !text.is_null() {
                let t = core::slice::from_raw_parts(text, PRINT_TEXT);
                print.text[..PRINT_TEXT].copy_from_slice(t);
            }
            let use_print = mode == 0 && (dust != 0 || stamp != 0);
            if let Err(e) = pipeline::render(&mut s.work, s.width, s.height, lut, &fx,
                                             if use_print { Some(&print) } else { None }) {
                return e;
            }
        }
        pipeline::to_rgba(&s.work, &mut s.rgba);
        0
    })
}

/// Keep PRINT.BIN for the Print engine. Returns 0, or -1 when it is not one.
/// # Safety
/// `p` must point to `n` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn fl_set_print_data(p: *const u8, n: usize) -> i32 {
    let data = core::slice::from_raw_parts(p, n);
    if !engines::print_data_ok(data) {
        return -1;
    }
    STATE.with(|s| s.borrow_mut().print_data = data.to_vec());
    0
}

/// Render one of the other engines into the RGBA output. `engine` 1:
/// ZrnoBoy (7 setting bytes), 2: Print (9), 3: Grade (40 signed). `seed` is
/// the grain seed; tv96, av96, iso and ev96 are what the Viewfinder frame
/// prints (av96 0: none known, the kernel's default is printed). Returns 0, -10 with nothing loaded, -12 without PRINT.BIN, -13
/// for an unknown engine or settings out of range, or the kernel's error.
/// # Safety
/// `settings` must point to the engine's setting bytes.
#[no_mangle]
pub unsafe extern "C" fn fl_render_engine(
    engine: u32, settings: *const u8, seed: u32, tv96: i32, av96: i32, iso: i32, ev96: i32,
) -> i32 {
    STATE.with(|s| {
        let s = &mut *s.borrow_mut();
        if s.base.is_empty() {
            return -10;
        }
        s.work.clear();
        s.work.extend_from_slice(&s.base);
        let rc = match engine {
            1 => {
                let mut v = [0u8; 7];
                v.copy_from_slice(core::slice::from_raw_parts(settings, 7));
                // av96 0 (F1.0) means the JPEG gave no exposure.
                let ex = engines::Exposure { tv96: tv96 as i16, av96: av96 as i16, iso: iso as i16, ev96: ev96 as i16 };
                let ex = if av96 > 0 { Some(&ex) } else { None };
                engines::gboy(&mut s.work, s.width, s.height, &engines::GboySettings(v), seed, ex)
            }
            2 => {
                if s.print_data.is_empty() {
                    return -12;
                }
                let mut v = [0u8; 9];
                v.copy_from_slice(core::slice::from_raw_parts(settings, 9));
                engines::print(&mut s.work, s.width, s.height, &s.print_data, &engines::RisoSettings(v), seed)
            }
            3 => {
                let mut v = [0i8; 40];
                v.copy_from_slice(core::slice::from_raw_parts(settings as *const i8, 40));
                let Some(table) = engines::grade_table(&engines::GradeSettings(v)) else { return -13 };
                engines::apply_table(&mut s.work, &table);
                0
            }
            _ => -13,
        };
        if rc != 0 {
            return rc;
        }
        pipeline::to_rgba(&s.work, &mut s.rgba);
        0
    })
}

/// The engines' labels as JSON at `fl_output()`-like memory: returns a
/// pointer to NUL-terminated UTF-8 (kept until the next call).
#[no_mangle]
pub extern "C" fn fl_names() -> *const u8 {
    thread_local! { static NAMES: RefCell<Vec<u8>> = RefCell::new(Vec::new()); }
    NAMES.with(|n| {
        let mut n = n.borrow_mut();
        *n = engines::names_json().into_bytes();
        n.push(0);
        n.as_ptr()
    })
}
