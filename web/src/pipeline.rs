// SPDX-License-Identifier: GPL-2.0-or-later
//! The camera's effect kernels (C, compiled unchanged) run in the same order
//! and with the same arguments as camera/film_stage.c: the colour pass (toy
//! lens on input rows, LUT, halation, vignette, light leaks, then dust and the
//! date stamp), then soft focus, then Bloom, then grain.

use core::ffi::c_void;

pub const PRINT_TEXT: usize = 24;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PrintSettings {
    pub dust: u32,
    pub dust_dark: u32,
    pub stamp: u32,
    pub text: [u8; PRINT_TEXT + 1],
}

type Progress = Option<extern "C" fn(u32, u32)>;

extern "C" {
    fn film_lut_prepare(lut: *mut c_void, size: usize) -> i32;
    fn film_halo_level_work_size(width: u32, level: u32) -> usize;
    fn film_toy_work_size(width: u32) -> usize;
    fn film_bloom_work_size(width: u32) -> usize;
    fn film_grain_work_size(width: u32) -> usize;
    fn film_uyvy_map_cached_retro(
        image: *mut u8, size: usize, width: u32, height: u32, stride: u32, lut: *const c_void,
        scratch: *mut u8, capacity: usize, progress: Progress, vignette: u32, leaks: u32, seed: u32,
        toy: u32, toy_work: *mut c_void, toy_size: usize, print: *const PrintSettings,
    ) -> i32;
    fn film_uyvy_map_halo_cached_retro(
        image: *mut u8, size: usize, width: u32, height: u32, stride: u32, lut: *const c_void,
        level: u32, work: *mut c_void, work_size: usize, scratch: *mut u8, capacity: usize,
        progress: Progress, vignette: u32, leaks: u32, seed: u32, toy: u32, toy_work: *mut c_void,
        toy_size: usize, print: *const PrintSettings,
    ) -> i32;
    fn film_uyvy_bloom(
        image: *mut u8, size: usize, width: u32, height: u32, stride: u32, level: u32,
        work: *mut c_void, work_size: usize, progress: Progress,
    ) -> i32;
    fn film_uyvy_soften(
        image: *mut u8, size: usize, width: u32, height: u32, stride: u32, level: u32,
        work: *mut c_void, work_size: usize, progress: Progress,
    ) -> i32;
    fn film_uyvy_grain(
        image: *mut u8, size: usize, width: u32, height: u32, stride: u32, level: u32, seed: u32,
        work: *mut c_void, work_size: usize, progress: Progress,
    ) -> i32;
}

/// A prepared LUT (the C kernels' G12LUTP form); 4-byte aligned storage.
pub struct Lut {
    words: Vec<u32>,
}

impl Lut {
    pub fn new(bytes: &[u8]) -> Result<Lut, i32> {
        let mut words = vec![0u32; (bytes.len() + 3) / 4];
        unsafe {
            core::ptr::copy_nonoverlapping(bytes.as_ptr(), words.as_mut_ptr() as *mut u8, bytes.len());
            let rc = film_lut_prepare(words.as_mut_ptr() as *mut c_void, bytes.len());
            if rc != 0 {
                return Err(rc);
            }
        }
        Ok(Lut { words })
    }
    fn ptr(&self) -> *const c_void {
        self.words.as_ptr() as *const c_void
    }
}

#[derive(Clone, Copy, Default)]
pub struct Effects {
    pub halation: u32,
    pub bloom: u32,
    pub grain: u32,
    pub vignette: u32,
    pub leaks: u32,
    pub toy: u32,
    pub soft: u32,
    /// Grain seed; light leaks use seed ^ 0xa3b195f1 as on the camera.
    pub seed: u32,
}

/// Grade `image` (signed UYVY, width x height) in place.
pub fn render(image: &mut [u8], width: usize, height: usize, lut: &Lut, fx: &Effects,
              print: Option<&PrintSettings>) -> Result<(), i32> {
    let (w, h, stride) = (width as u32, height as u32, (width * 2) as u32);
    let size = image.len();
    let mut scratch = vec![0u8; 16384];
    let retro_seed = fx.seed ^ 0xa3b1_95f1;
    let print_ptr = print.map_or(core::ptr::null(), |p| p as *const PrintSettings);
    unsafe {
        let mut toy_work = vec![0u32; if fx.toy > 0 { film_toy_work_size(w) / 4 + 1 } else { 0 }];
        let toy_size = toy_work.len() * 4;
        let toy_ptr = if fx.toy > 0 { toy_work.as_mut_ptr() as *mut c_void } else { core::ptr::null_mut() };
        let rc = if fx.halation > 0 {
            let n = film_halo_level_work_size(w, fx.halation);
            let mut halo = vec![0u32; n / 4 + 1];
            film_uyvy_map_halo_cached_retro(
                image.as_mut_ptr(), size, w, h, stride, lut.ptr(), fx.halation,
                halo.as_mut_ptr() as *mut c_void, n, scratch.as_mut_ptr(), scratch.len(), None,
                fx.vignette, fx.leaks, retro_seed, fx.toy, toy_ptr, toy_size, print_ptr)
        } else {
            film_uyvy_map_cached_retro(
                image.as_mut_ptr(), size, w, h, stride, lut.ptr(), scratch.as_mut_ptr(), scratch.len(),
                None, fx.vignette, fx.leaks, retro_seed, fx.toy, toy_ptr, toy_size, print_ptr)
        };
        if rc != 0 {
            return Err(rc);
        }
        if fx.soft > 0 {
            let n = film_bloom_work_size(w);
            let mut work = vec![0u32; n / 4 + 1];
            let rc = film_uyvy_soften(image.as_mut_ptr(), size, w, h, stride, fx.soft,
                                      work.as_mut_ptr() as *mut c_void, n, None);
            if rc != 0 {
                return Err(rc);
            }
        }
        if fx.bloom > 0 {
            let n = film_bloom_work_size(w);
            let mut work = vec![0u32; n / 4 + 1];
            let rc = film_uyvy_bloom(image.as_mut_ptr(), size, w, h, stride, fx.bloom,
                                     work.as_mut_ptr() as *mut c_void, n, None);
            if rc != 0 {
                return Err(rc);
            }
        }
        if fx.grain > 0 {
            let n = film_grain_work_size(w);
            let mut work = vec![0u32; n / 4 + 1];
            let rc = film_uyvy_grain(image.as_mut_ptr(), size, w, h, stride, fx.grain, fx.seed,
                                     work.as_mut_ptr() as *mut c_void, n, None);
            if rc != 0 {
                return Err(rc);
            }
        }
    }
    Ok(())
}

/// Signed UYVY to RGBA (full-range BT.601), as tools/acceptance.py shows it.
pub fn to_rgba(uyvy: &[u8], out: &mut [u8]) {
    for (p, q) in uyvy.chunks_exact(4).zip(out.chunks_exact_mut(8)) {
        let u = p[0] as i8 as f32;
        let v = p[2] as i8 as f32;
        let (rv, gu, gv, bu) = (1.402 * v, 0.344_136 * u, 0.714_136 * v, 1.772 * u);
        for j in 0..2 {
            let y = p[1 + 2 * j] as f32;
            let o = &mut q[4 * j..4 * j + 4];
            o[0] = (y + rv + 0.5).clamp(0.0, 255.0) as u8;
            o[1] = (y - gu - gv + 0.5).clamp(0.0, 255.0) as u8;
            o[2] = (y + bu + 0.5).clamp(0.0, 255.0) as u8;
            o[3] = 255;
        }
    }
}

/// RGBA (from a decoded JPEG or PNG) to signed UYVY, full-range BT.601.
pub fn from_rgba(rgba: &[u8], width: usize, height: usize) -> Vec<u8> {
    let mut out = vec![0u8; width * height * 2];
    for (p, q) in rgba.chunks_exact(8).zip(out.chunks_exact_mut(4)) {
        let (mut cb, mut cr) = (0f32, 0f32);
        for j in 0..2 {
            let (r, g, b) = (p[4 * j] as f32, p[4 * j + 1] as f32, p[4 * j + 2] as f32);
            q[1 + 2 * j] = (0.299 * r + 0.587 * g + 0.114 * b + 0.5) as u8;
            cb += -0.168_736 * r - 0.331_264 * g + 0.5 * b;
            cr += 0.5 * r - 0.418_688 * g - 0.081_312 * b;
        }
        q[0] = ((cb / 2.0).round().clamp(-128.0, 127.0) as i8) as u8;
        q[2] = ((cr / 2.0).round().clamp(-128.0, 127.0) as i8) as u8;
    }
    out
}
