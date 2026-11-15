#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Talk to the G12 over USB through CHDK's PTP extension (chdkptp).

The card stays in the camera. Needs the chdkptp CLI built at ~/dev/chdkptp
(override with CHDKPTP=/path/to/chdkptp.sh) and the camera switched on with
the Zrno CHDK build running.

  status                 camera, CHDK build, mode, battery, free space
  ls [DIR]               list a card directory (default A/CHDK/FILM)
  logs [DEST]            download A/CHDK/FILM/*.TXT (default build/usb-logs/<time>)
  get SRC... DEST        download files or directories
  push SRC... DIR        upload files to a card directory
  screen [OUT]           grab live view + CHDK/Canon overlay as PNGs
  lua CODE               run Lua on the camera and print the result
  rec | play             switch to shooting / playback mode (USB opens the gallery)
  reboot [FILE]          restart CHDK, optionally booting FILE (e.g. A/DISKBOOT.BIN)
  raw CMD...             pass commands straight to chdkptp

macOS's ptpcamerad claims Canon cameras when they appear; it is stopped
before each connection (it is a per-user agent and launchd restarts it).
"""
import os, struct, subprocess, sys, time, zlib
from pathlib import Path

LAB = Path(__file__).resolve().parents[1]
CHDKPTP = os.environ.get('CHDKPTP', str(Path.home() / 'dev/chdkptp/chdkptp.sh'))


def stop_ptpcamerad():
    subprocess.run(['killall', '-9', 'ptpcamerad'], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)


def chdk(*cmds, tries=3, quiet=False):
    """Connect, run chdkptp CLI commands, return stdout. Raises on no camera."""
    for attempt in range(tries):
        stop_ptpcamerad()
        time.sleep(0.3)
        args = [CHDKPTP, '-c'] + [f'-e{c}' for c in cmds]
        p = subprocess.run(args, capture_output=True, text=True)
        out = p.stdout + p.stderr
        if 'not connected' not in out and 'no matching devices' not in out.lower():
            if not quiet:
                sys.stdout.write(out)
            return out
        time.sleep(1)
    sys.exit('No G12 found over USB: is it plugged in, switched on, and running CHDK?\n' + out.strip())


def png(path, w, h, rows, alpha):
    raw = b''.join(b'\0' + r for r in rows)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    hdr = struct.pack('>IIBBBBB', w, h, 8, 6 if alpha else 2, 0, 0, 0)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', hdr) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def read_netpbm(path):
    """Read P6 (ppm) or P7 (pam RGB_ALPHA). Returns w, h, channels, bytes."""
    data = Path(path).read_bytes()
    if data[:2] == b'P6':
        parts, i = [], 2
        while len(parts) < 3:
            while data[i:i + 1].isspace(): i += 1
            j = i
            while not data[j:j + 1].isspace(): j += 1
            parts.append(int(data[i:j])); i = j
        return parts[0], parts[1], 3, data[i + 1:]
    if data[:2] == b'P7':
        end = data.index(b'ENDHDR\n') + 7
        hdr = dict(l.split(b' ', 1) for l in data[3:end - 7].split(b'\n') if b' ' in l)
        return int(hdr[b'WIDTH']), int(hdr[b'HEIGHT']), int(hdr[b'DEPTH']), data[end:]
    raise ValueError(f'{path}: not a P6/P7 netpbm file')


def to_png(src, dst):
    w, h, c, px = read_netpbm(src)
    png(dst, w, h, [px[y * w * c:(y + 1) * w * c] for y in range(h)], c == 4)
    return w, h, c, px


def composite(vp, bm, dst):
    """Scale both layers (nearest) to the larger size and blend the overlay on top.
    The G12's overlay is 720x240 with 2:1 tall pixels; the viewport is 720x480."""
    vw, vh, _, vpx = vp
    bw, bh, _, bpx = bm
    w, h = max(vw, bw), max(vh, bh)
    rows = []
    for y in range(h):
        vy, by = y * vh // h, y * bh // h
        row = bytearray(w * 3)
        for x in range(w):
            o = (vy * vw + x * vw // w) * 3
            b = (by * bw + x * bw // w) * 4
            a = bpx[b + 3]
            for k in range(3):
                row[x * 3 + k] = (bpx[b + k] * a + vpx[o + k] * (255 - a)) // 255
        rows.append(bytes(row))
    png(dst, w, h, rows, False)


def main(argv):
    if not argv or argv[0] in ('-h', '--help', 'help'):
        print(__doc__); return
    cmd, rest = argv[0], argv[1:]
    stamp = time.strftime('%Y%m%d-%H%M%S')
    if cmd == 'status':
        chdk('luar local b=get_buildinfo() local r,v,m=get_mode() '
             'return string.format("%s %s | CHDK %s r%s %s | rec=%s mode=0x%x | battery %d mV | free %d KiB",'
             'b.platform, b.platsub, b.build_number, tostring(b.build_revision), b.build_date,'
             'tostring(r), m, get_vbatt(), get_free_disk_space())')
    elif cmd == 'ls':
        chdk('ls -l ' + (rest[0] if rest else 'A/CHDK/FILM'))
    elif cmd == 'logs':
        dest = Path(rest[0]) if rest else LAB / 'build/usb-logs' / stamp
        dest.mkdir(parents=True, exist_ok=True)
        # top level only, no macOS ._ resource-fork files
        chdk(f'mdl -fmatch=/[^./][^/]*%.TXT$ -maxdepth=1 -nodirs -overwrite=y A/CHDK/FILM "{dest}"')
        print(f'logs -> {dest}')
    elif cmd == 'get':
        if len(rest) < 2: sys.exit('get SRC... DEST')
        chdk('mdl -overwrite=y ' + ' '.join(f'"{s}"' for s in rest))
    elif cmd == 'push':
        if len(rest) < 2: sys.exit('push SRC... DIR')
        chdk('mup ' + ' '.join(f'"{s}"' for s in rest))
    elif cmd == 'screen':
        out = Path(rest[0]) if rest else LAB / 'build/usb-screens' / stamp
        out.mkdir(parents=True, exist_ok=True)
        # no -nosubst: it crashes chdkptp r1528 (lvutil.lua:957); these paths have no ${} patterns
        chdk(f'lvdumpimg -count=1 -vp="{out}/vp.ppm" -bm="{out}/ui.pam"', quiet=True)
        vp = to_png(out / 'vp.ppm', out / 'viewport.png')
        bm = to_png(out / 'ui.pam', out / 'overlay.png')
        composite(vp, bm, out / 'screen.png')
        print(f'{out}/screen.png  (viewport {vp[0]}x{vp[1]}, overlay {bm[0]}x{bm[1]})')
    elif cmd == 'lua':
        chdk('luar ' + ' '.join(rest))
    elif cmd in ('rec', 'play'):
        # Canon drops to the gallery whenever USB is plugged in; 'rec' switches
        # back to shooting (CHDK switch_mode_usb) and waits until it has.
        want = 'true' if cmd == 'rec' else 'false'
        chdk(cmd, 'luar local t=get_tick_count() repeat local r=get_mode() '
             f'if tostring(r)=="{want}" then return "{cmd} mode after "..(get_tick_count()-t).." ms" end sleep(100) '
             f'until get_tick_count()-t>5000 return "still not in {cmd} mode after 5 s"')
    elif cmd == 'reboot':
        chdk('reboot' + (' ' + rest[0] if rest else ''))
    elif cmd == 'raw':
        chdk(*rest)
    else:
        sys.exit(f'unknown command {cmd!r}; try --help')


if __name__ == '__main__':
    main(sys.argv[1:])
