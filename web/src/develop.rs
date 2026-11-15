// SPDX-License-Identifier: GPL-2.0-or-later
//! G12 raw development to the frame the camera grades: 3648 x 2736 signed
//! UYVY422 (full-range BT.601), cropped where Canon crops its JPEG. Follows
//! LibRaw's default order and constants (as rawpy uses them for the repository's
//! acceptance renders): per-site black level, white balance scaled so the
//! smallest multiplier is 1, demosaic, camera-to-sRGB matrix, auto-brightness
//! putting the brightest 1 % at white, and the BT.709 tone curve. Demosaic is
//! Malvar-He-Cutler gradient-corrected linear interpolation.

/// A G12 sensor frame: 12-bit samples, RGGB.
pub struct Raw {
    pub width: usize,
    pub height: usize,
    pub data: Vec<u16>,
    /// White balance as R G G B levels; all zero means daylight.
    pub wb: [u16; 4],
}

pub const WIDTH: usize = 3648;
pub const HEIGHT: usize = 2736;
pub const RAW_W: usize = 3744;
pub const RAW_H: usize = 2784;
const CROP_X: usize = 72; // JPEG area in raw coordinates (even: the CFA stays RGGB)
const CROP_Y: usize = 24;
const WHITE: f32 = 4095.0;
/// Camera RGB to linear sRGB for the G12 (LibRaw's Adobe matrix through XYZ D65).
const RGB_CAM: [[f32; 3]; 3] = [
    [1.3862, -0.176485, -0.209715],
    [-0.319009, 1.770466, -0.451458],
    [0.024024, -0.499213, 1.475189],
];

/// CFA colour at raw (y, x): 0 R, 1 G, 2 B.
#[inline]
fn cfa(y: usize, x: usize) -> usize {
    match (y & 1, x & 1) {
        (0, 0) => 0,
        (1, 1) => 2,
        _ => 1,
    }
}

/// Black level per CFA site (R, G1, G2, B) from the masked left border (columns 0-47).
fn black_levels(raw: &Raw) -> [f32; 4] {
    let mut sum = [0f64; 4];
    let mut n = [0f64; 4];
    for y in 12..RAW_H - 12 {
        for x in 0..48 {
            let k = (y & 1) * 2 + (x & 1);
            sum[k] += raw.data[y * RAW_W + x] as f64;
            n[k] += 1.0;
        }
    }
    core::array::from_fn(|k| (sum[k] / n[k]) as f32)
}

/// Grey-world white balance over the JPEG area, as R G G B levels with green
/// at 1024: the levels that make the frame's mean colour neutral. Clipped
/// sites (and their 2 x 2 cell) are left out, so blown highlights don't pull
/// it. The backups carry no as-shot white balance of their own.
pub fn grey_world(raw: &Raw) -> [u16; 4] {
    let black = black_levels(raw);
    let clip = WHITE - 200.0;
    let mut sum = [0f64; 3];
    for y in (CROP_Y..CROP_Y + HEIGHT).step_by(2) {
        for x in (CROP_X..CROP_X + WIDTH).step_by(2) {
            let s = [
                raw.data[y * RAW_W + x],
                raw.data[y * RAW_W + x + 1],
                raw.data[(y + 1) * RAW_W + x],
                raw.data[(y + 1) * RAW_W + x + 1],
            ];
            if s.iter().any(|&v| v as f32 >= clip) {
                continue;
            }
            let v: [f32; 4] = core::array::from_fn(|k| (s[k] as f32 - black[k]).max(0.0));
            sum[0] += v[0] as f64;
            sum[1] += (v[1] + v[2]) as f64 / 2.0;
            sum[2] += v[3] as f64;
        }
    }
    if sum.iter().any(|&v| v <= 0.0) {
        return [0; 4];
    }
    let level = |c: usize| (1024.0 * sum[1] / sum[c]).round().clamp(1.0, 65535.0) as u16;
    [level(0), 1024, 1024, level(2)]
}

pub fn develop(raw: &Raw) -> Result<Vec<u8>, &'static str> {
    if raw.width != RAW_W || raw.height != RAW_H {
        return Err("not a PowerShot G12 raw frame (expected 3744 x 2784)");
    }
    let black = black_levels(raw);
    // White balance: R, G (mean of the two), B, scaled so the smallest is 1.
    let wb = raw.wb;
    let mut mul = if wb.iter().all(|&v| v > 0) {
        [wb[0] as f32, (wb[1] as f32 + wb[2] as f32) / 2.0, wb[3] as f32]
    } else {
        [1.745_76, 0.949_525, 1.792_049] // G12 daylight
    };
    let lo = mul.iter().cloned().fold(f32::MAX, f32::min);
    for m in &mut mul {
        *m /= lo;
    }
    let avg_black = (black[0] + black[1] + black[2] + black[3]) / 4.0;
    let scale: [f32; 3] = core::array::from_fn(|c| mul[c] * 65535.0 / (WHITE - avg_black));

    // Scaled linear plane for the crop plus a 2-pixel border.
    const B: usize = 2;
    let pw = WIDTH + 2 * B;
    let ph = HEIGHT + 2 * B;
    let mut plane = vec![0f32; pw * ph];
    for py in 0..ph {
        let y = CROP_Y + py - B;
        for px in 0..pw {
            let x = CROP_X + px - B;
            let c = cfa(y, x);
            let v = raw.data[y * RAW_W + x] as f32 - black[(y & 1) * 2 + (x & 1)];
            plane[py * pw + px] = (v.max(0.0) * scale[c]).min(65535.0);
        }
    }
    let at = |py: usize, px: usize| plane[py * pw + px];

    // Demosaic + matrix into 16-bit linear sRGB, with a histogram for auto-brightness.
    let mut rgb = vec![0u16; WIDTH * HEIGHT * 3];
    let mut hist = vec![[0u32; 0x2000]; 3];
    for oy in 0..HEIGHT {
        let py = oy + B;
        let y = CROP_Y + oy;
        for ox in 0..WIDTH {
            let px = ox + B;
            let x = CROP_X + ox;
            let c = at(py, px);
            let n = at(py - 1, px);
            let s = at(py + 1, px);
            let w = at(py, px - 1);
            let e = at(py, px + 1);
            let nn = at(py - 2, px);
            let ss = at(py + 2, px);
            let ww = at(py, px - 2);
            let ee = at(py, px + 2);
            let nw = at(py - 1, px - 1);
            let ne = at(py - 1, px + 1);
            let sw = at(py + 1, px - 1);
            let se = at(py + 1, px + 1);
            let cross = nn + ss + ww + ee;
            let (r, g, b) = match cfa(y, x) {
                0 | 2 => {
                    let g = (4.0 * c + 2.0 * (n + s + w + e) - cross) / 8.0;
                    let o = (6.0 * c + 2.0 * (nw + ne + sw + se) - 1.5 * cross) / 8.0;
                    if cfa(y, x) == 0 { (c, g, o) } else { (o, g, c) }
                }
                _ => {
                    let diag = nw + ne + sw + se;
                    // Colour of the horizontal neighbours vs the vertical ones.
                    let horiz = (5.0 * c + 4.0 * (w + e) - diag - (ww + ee) + 0.5 * (nn + ss)) / 8.0;
                    let vert = (5.0 * c + 4.0 * (n + s) - diag - (nn + ss) + 0.5 * (ww + ee)) / 8.0;
                    if y & 1 == 0 { (horiz, c, vert) } else { (vert, c, horiz) }
                }
            };
            let cam = [r.clamp(0.0, 65535.0), g.clamp(0.0, 65535.0), b.clamp(0.0, 65535.0)];
            let k = (oy * WIDTH + ox) * 3;
            for ch in 0..3 {
                let v = (RGB_CAM[ch][0] * cam[0] + RGB_CAM[ch][1] * cam[1] + RGB_CAM[ch][2] * cam[2])
                    .clamp(0.0, 65535.0) as u16;
                rgb[k + ch] = v;
                hist[ch][(v >> 3) as usize] += 1;
            }
        }
    }
    drop(plane);
    // Auto-brightness: the level above which 1 % of each channel lies.
    let perc = (WIDTH * HEIGHT) as u32 / 100;
    let mut white = 0usize;
    for h in &hist {
        let mut total = 0u32;
        let mut val = 0x2000;
        loop {
            val -= 1;
            if val <= 32 {
                break;
            }
            total += h[val];
            if total > perc {
                break;
            }
        }
        white = white.max(val);
    }
    let white = ((white << 3) as f32).max(1.0);
    // BT.709 curve to 8 bits.
    let curve: Vec<u8> = (0..0x10000)
        .map(|i| {
            let t = (i as f32 / white).min(1.0);
            let g = if t < 0.018 { 4.5 * t } else { 1.099 * t.powf(0.45) - 0.099 };
            (g * 255.0 + 0.5).clamp(0.0, 255.0) as u8
        })
        .collect();
    // Full-range YCbCr, chroma averaged per pixel pair, signed.
    let mut uyvy = vec![0u8; WIDTH * HEIGHT * 2];
    for p in 0..WIDTH * HEIGHT / 2 {
        let mut ys = [0u8; 2];
        let (mut cb, mut cr) = (0f32, 0f32);
        for j in 0..2 {
            let k = (2 * p + j) * 3;
            let (r, g, b) = (curve[rgb[k] as usize] as f32, curve[rgb[k + 1] as usize] as f32, curve[rgb[k + 2] as usize] as f32);
            ys[j] = (0.299 * r + 0.587 * g + 0.114 * b + 0.5) as u8;
            cb += -0.168_736 * r - 0.331_264 * g + 0.5 * b;
            cr += 0.5 * r - 0.418_688 * g - 0.081_312 * b;
        }
        let q = &mut uyvy[4 * p..4 * p + 4];
        q[0] = ((cb / 2.0).round().clamp(-128.0, 127.0) as i8) as u8;
        q[1] = ys[0];
        q[2] = ((cr / 2.0).round().clamp(-128.0, 127.0) as i8) as u8;
        q[3] = ys[1];
    }
    Ok(uyvy)
}
