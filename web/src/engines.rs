// SPDX-License-Identifier: GPL-2.0-or-later
//! The camera's other engines on a developed frame: ZrnoBoy
//! (`film_gboy.c`), Print (`film_riso.c`) and Grade (`film_grade.c`), the
//! kernels unchanged. The calls follow the camera's own (`film_gbui.c`
//! `film_gbui_render`, `film_rsui.c`). Grade builds the DIGIC colour table
//! the camera installs; Canon's develop applies it in hardware, so it is
//! applied here as `tools/acceptance.py` `digic_grade` emulates it.

use core::ffi::c_void;

/// `film_gboy_settings`: palette, frame, contrast, brightness, edges,
/// dither, resolution.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct GboySettings(pub [u8; 7]);

/// `film_riso_settings`: inks, separation, screen, noise, register, flaws,
/// paper, style, variant.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct RisoSettings(pub [u8; 9]);

/// `film_grade_settings`: 40 signed fields in `film_grade.h` order.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GradeSettings(pub [i8; 40]);

#[repr(C)]
#[derive(Default)]
struct Geom {
    cw: u32,
    ch: u32,
    wx: u32,
    wy: u32,
    ww: u32,
    wh: u32,
}

#[repr(C)]
#[derive(Default)]
struct Levels {
    lo: i32,
    hi: i32,
}

/// `film_gboy_exposure` (APEX96, market ISO), printed by the Viewfinder frame.
#[repr(C)]
pub struct Exposure {
    pub tv96: i16,
    pub av96: i16,
    pub iso: i16,
    pub ev96: i16,
}

type Progress = Option<unsafe extern "C" fn(u32, u32)>;

extern "C" {
    fn film_gboy_geometry(frame: u32, resolution: u32, g: *mut Geom);
    fn film_gboy_frame(frame: u32, resolution: u32, canvas: *mut u8, seed: u32, exposure: *const Exposure);
    fn film_gboy_sample_uyvy(frame: *const u8, width: u32, height: u32, stride: u32, ww: u32, wh: u32, step: u32, luma: *mut u8);
    fn film_gboy_levels_measure(luma: *const u8, n: u32, lv: *mut Levels);
    fn film_gboy_picture(luma: *const u8, work: *mut u8, s: *const GboySettings, g: *const Geom, lv: *const Levels,
                         seed: u32, canvas: *mut u8);
    fn film_gboy_paint_uyvy(frame: *mut u8, width: u32, height: u32, stride: u32, canvas: *const u8, g: *const Geom,
                            palette: u32, row: *mut u8, progress: Progress) -> i32;
    fn film_riso_data_check(data: *const u8, size: usize) -> i32;
    fn film_riso_work_size(width: u32) -> usize;
    fn film_riso_render(frame: *mut u8, width: u32, height: u32, stride: u32, data: *const u8, s: *const RisoSettings,
                        seed: u32, work: *mut c_void, work_bytes: usize, progress: Progress) -> i32;
    fn film_grade_valid(s: *const GradeSettings) -> i32;
    fn film_gboy_palette_name(v: u32) -> *const u8;
    fn film_gboy_frame_name(v: u32) -> *const u8;
    fn film_riso_style_name(v: u32) -> *const u8;
    fn film_riso_variants(style: u32) -> u32;
    fn film_riso_variant_name(style: u32, v: u32) -> *const u8;
    fn film_riso_inks_name(v: u32) -> *const u8;
    fn film_riso_separation_name(v: u32) -> *const u8;
    fn film_riso_screen_name(v: u32) -> *const u8;
    fn film_riso_noise_name(v: u32) -> *const u8;
    fn film_riso_register_name(v: u32) -> *const u8;
    fn film_riso_flaws_name(v: u32) -> *const u8;
    fn film_riso_paper_name(v: u32) -> *const u8;
    fn film_grade_hue_name(v: u32) -> *const u8;
    fn film_grade_mono_name(v: u32) -> *const u8;
    fn film_grade_min(field: u32) -> i32;
    fn film_grade_max(field: u32) -> i32;
    fn film_grade_table(s: *const GradeSettings, table: *mut u32);
}

/// `FILM_GBOY_CANVAS_BYTES`: 320 x 240.
const GBOY_CANVAS: usize = 320 * 240;

/// ZrnoBoy over a signed UYVY frame (stride width * 2), in place. `seed` is
/// the camera's grain seed; the engines get it XOR 0x5bd1e995 as on the camera.
/// Without an exposure the Viewfinder frame prints the kernel's default
/// (1/125, F8.0, ISO 400).
pub fn gboy(image: &mut [u8], width: usize, height: usize, s: &GboySettings, seed: u32, ex: Option<&Exposure>) -> i32 {
    let (w, h, stride) = (width as u32, height as u32, (width * 2) as u32);
    let mut g = Geom::default();
    let mut lv = Levels::default();
    let seed = seed ^ 0x5bd1_e995;
    unsafe {
        film_gboy_geometry(s.0[1] as u32, s.0[6] as u32, &mut g);
        let n = (g.ww * g.wh) as usize;
        let mut canvas = vec![0u8; GBOY_CANVAS];
        let mut luma = vec![0u8; n];
        let mut work = vec![0u8; n * 2];
        let mut row = vec![0u8; width * 2];
        film_gboy_frame(s.0[1] as u32, s.0[6] as u32, canvas.as_mut_ptr(), seed,
                        ex.map_or(core::ptr::null(), |e| e as *const Exposure));
        film_gboy_sample_uyvy(image.as_ptr(), w, h, stride, g.ww, g.wh, if width >= 2400 { 2 } else { 1 }, luma.as_mut_ptr());
        film_gboy_levels_measure(luma.as_ptr(), g.ww * g.wh, &mut lv);
        film_gboy_picture(luma.as_ptr(), work.as_mut_ptr(), s, &g, &lv, seed, canvas.as_mut_ptr());
        film_gboy_paint_uyvy(image.as_mut_ptr(), w, h, stride, canvas.as_ptr(), &g, s.0[0] as u32, row.as_mut_ptr(), None)
    }
}

/// Whether `data` is a valid PRINT.BIN.
pub fn print_data_ok(data: &[u8]) -> bool {
    unsafe { film_riso_data_check(data.as_ptr(), data.len()) == 0 }
}

/// Print over a signed UYVY frame, in place. Width must be a multiple of 4.
pub fn print(image: &mut [u8], width: usize, height: usize, data: &[u8], s: &RisoSettings, seed: u32) -> i32 {
    if width % 4 != 0 {
        return -1;
    }
    unsafe {
        let bytes = film_riso_work_size(width as u32) + 16;
        let mut work = vec![0u64; bytes / 8 + 1];
        film_riso_render(image.as_mut_ptr(), width as u32, height as u32, (width * 2) as u32, data.as_ptr(), s,
                         seed ^ 0x5bd1_e995, work.as_mut_ptr() as *mut c_void, bytes, None)
    }
}

/// The grade's 1000-word DIGIC colour table, or None when a field is out of range.
pub fn grade_table(s: &GradeSettings) -> Option<Vec<u32>> {
    unsafe {
        if film_grade_valid(s) == 0 {
            return None;
        }
        let mut t = vec![0u32; 1000];
        film_grade_table(s, t.as_mut_ptr());
        Some(t)
    }
}

/// One node of a packed table, 8-bit scale Y, U, V.
fn node(table: &[u32], i: usize) -> [f32; 3] {
    let d = table[i];
    let y = ((d >> 20) & 0x3ff) as i32;
    let mut u = ((d >> 10) & 0x3ff) as i32;
    let mut v = (d & 0x3ff) as i32;
    if u >= 512 { u -= 1024; }
    if v >= 512 { v -= 1024; }
    [y as f32 / 2.0, u as f32 / 2.0, v as f32 / 2.0]
}

/// The hardware's tetrahedral interpolation (tools/hw_table.py `tetra` and
/// `apply_yuv`): cells of 64 on the 9-bit input, the table indexed
/// r + 10g + 100b.
fn table_yuv(table: &[u32], rgb: [i32; 3]) -> [f32; 3] {
    let mut cell = [0usize; 3];
    let mut frac = [0f32; 3];
    for c in 0..3 {
        let t = rgb[c] * 2;
        let k = (t >> 6).min(7);
        cell[c] = k as usize;
        frac[c] = (t - k * 64) as f32 / 64.0;
    }
    const STEP: [usize; 3] = [1, 10, 100];
    // Channels by falling fraction; ties keep r, g, b order (a stable sort).
    let mut order = [0usize, 1, 2];
    order.sort_by(|&a, &b| frac[b].partial_cmp(&frac[a]).unwrap_or(core::cmp::Ordering::Equal));
    let base = cell[0] + 10 * cell[1] + 100 * cell[2];
    let i1 = base + STEP[order[0]];
    let i2 = i1 + STEP[order[1]];
    let i3 = i2 + STEP[order[2]];
    let f = [frac[order[0]], frac[order[1]], frac[order[2]]];
    let w = [1.0 - f[0], f[0] - f[1], f[1] - f[2], f[2]];
    let mut out = [0f32; 3];
    for (idx, wt) in [base, i1, i2, i3].into_iter().zip(w) {
        let n = node(table, idx);
        for c in 0..3 {
            out[c] += wt * n[c];
        }
    }
    out
}

/// Applies a colour table to a signed UYVY frame, in place, as Canon's
/// develop would (tools/acceptance.py `digic_grade`): each pixel decoded to
/// RGB, the table's Y kept per pixel, U and V averaged over the pair.
pub fn apply_table(image: &mut [u8], table: &[u32]) {
    for px in image.chunks_exact_mut(4) {
        let u = px[0] as i8 as f32;
        let v = px[2] as i8 as f32;
        let mut yuv = [[0f32; 3]; 2];
        for (k, y_at) in [1usize, 3].into_iter().enumerate() {
            let y = px[y_at] as f32;
            let rgb = [y + 1.402 * v, y - 0.344_136 * u - 0.714_136 * v, y + 1.772 * u]
                .map(|c| ((c + 0.5).floor() as i32).clamp(0, 255));
            yuv[k] = table_yuv(table, rgb);
        }
        px[1] = yuv[0][0].round().clamp(0.0, 255.0) as u8;
        px[3] = yuv[1][0].round().clamp(0.0, 255.0) as u8;
        px[0] = (((yuv[0][1] + yuv[1][1]) / 2.0).round().clamp(-128.0, 127.0) as i8) as u8;
        px[2] = (((yuv[0][2] + yuv[1][2]) / 2.0).round().clamp(-128.0, 127.0) as i8) as u8;
    }
}

fn c_str(p: *const u8) -> String {
    if p.is_null() {
        return String::new();
    }
    let mut n = 0;
    unsafe {
        while *p.add(n) != 0 {
            n += 1;
        }
        String::from_utf8_lossy(core::slice::from_raw_parts(p, n)).into_owned()
    }
}

fn quote(s: &str) -> String {
    let mut o = String::from("\"");
    for c in s.chars() {
        match c {
            '"' | '\\' => { o.push('\\'); o.push(c); }
            c if (c as u32) < 32 => {}
            c => o.push(c),
        }
    }
    o.push('"');
    o
}

fn list(n: u32, f: impl Fn(u32) -> *const u8) -> String {
    let items: Vec<String> = (0..n).map(|i| quote(&c_str(f(i)))).collect();
    format!("[{}]", items.join(","))
}

/// The engines' menu labels and the grade's field ranges as JSON, from the
/// kernels themselves so the page shows the camera's words.
pub fn names_json() -> String {
    unsafe {
        let variants: Vec<String> = (0..7).map(|st| list(film_riso_variants(st), |v| film_riso_variant_name(st, v))).collect();
        let range: Vec<String> = (0..40).map(|f| format!("[{},{}]", film_grade_min(f), film_grade_max(f))).collect();
        format!(
            "{{\"palettes\":{},\"frames\":{},\"styles\":{},\"variants\":[{}],\"inks\":{},\"separation\":{},\"screen\":{},\"noise\":{},\"register\":{},\"flaws\":{},\"paper\":{},\"hues\":{},\"monos\":{},\"gradeRange\":[{}]}}",
            list(10, |v| film_gboy_palette_name(v)), list(11, |v| film_gboy_frame_name(v)), list(7, |v| film_riso_style_name(v)),
            variants.join(","), list(7, |v| film_riso_inks_name(v)), list(2, |v| film_riso_separation_name(v)),
            list(2, |v| film_riso_screen_name(v)), list(5, |v| film_riso_noise_name(v)), list(4, |v| film_riso_register_name(v)),
            list(4, |v| film_riso_flaws_name(v)), list(3, |v| film_riso_paper_name(v)), list(8, |v| film_grade_hue_name(v)),
            list(7, |v| film_grade_mono_name(v)), range.join(","))
    }
}
