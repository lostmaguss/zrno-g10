#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Simulate the camera's Zrno menu on the Mac.

CHDK's own gui_menu.c, gui_draw.c and rbf_font.c (with tools/menu_patch.py
applied, as on the camera) run together with camera/film_menu.c and
camera/film_lut.c; the shooting screen is CHDK's own OSD code (gui_osd.c and
its configuration defaults, with tools/osd_patch.py applied). The screen is the G12's 360 x 240 CHDK view, shown 4:3.

  python3 tools/menu_sim.py serve [--port 8766]
      Interactive page at http://localhost:8766. Arrow keys, Enter (SET),
      M (MENU), S (S button). Edit camera/film_menu.c and the page rebuilds
      and replays your key presses on the next press or reload.
  python3 tools/menu_sim.py shot OUT.png [KEY ...]
      Press the keys from the S button start and save the screen.

Options: --font NAME uses CHDK/FONTS/NAME.rbf from the CHDK tree instead of
card/CHDK/FILM/MENU.RBF; --root starts in CHDK's main menu.
Needs the unmodified CHDK r6355 tree (build/chdk-r6355 from tools/fetch_chdk.py, or $CHDK_BASE).
"""
import argparse, base64, urllib.parse, http.server, json, os, shutil, struct, subprocess, sys, tempfile, threading, zlib
from pathlib import Path

LAB = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LAB / 'tools'))
from menu_patch import menu_edits
from osd_patch import osd_edits, OSD_OFF

OUT = LAB / 'build' / 'menu-sim'
BIN = OUT / 'menu_sim'
W, H = 360, 240
SOURCES = [LAB / 'camera' / n for n in ('film_menu.c', 'film_menu.h', 'film_lut.c', 'film_lut.h', 'film_ui.c', 'film_ui.h', 'film_hwcolour.c', 'film_hwcolour.h',
                                        'film_gboy.c', 'film_gboy.h', 'film_gbui.c', 'film_gbui.h',
                                        'film_riso.c', 'film_riso.h', 'film_rsui.c', 'film_rsui.h',
                                        'film_grade.c', 'film_grade.h', 'film_grui.c', 'film_grui.h')] + \
          [LAB / 'tools' / 'menu_patch.py', LAB / 'tools' / 'osd_patch.py'] + sorted((LAB / 'tools' / 'menu_sim').glob('*.c'))


def chdk_base():
    env = os.environ.get('CHDK_BASE')
    for c in ([Path(env)] if env else []) + [LAB / 'build' / 'chdk-r6355']:
        if (c / 'core' / 'gui_menu.c').exists():
            return c
    raise SystemExit('CHDK r6355 tree not found: run tools/fetch_chdk.py or set CHDK_BASE')


# MENU_SIM_ASAN=1: build with AddressSanitizer (memory errors stop the
# simulator with a report on stderr).
SANITIZE = ['-fsanitize=address', '-fno-omit-frame-pointer'] if os.environ.get('MENU_SIM_ASAN') == '1' else []


def build():
    base = chdk_base()
    tree = OUT / 'chdk'
    shutil.rmtree(tree, ignore_errors=True)
    for d in ('core', 'include', 'lib/font', 'platform/g12', 'modules'):
        shutil.copytree(base / d, tree / d, ignore=shutil.ignore_patterns('*.o', '*.a', '.o'))
    # CHDK's make generates the packed 8x16 font; an unbuilt tree lacks it.
    packed = tree / 'lib/font/font_8x16_uni_packed.h'
    if not packed.exists():
        packer = OUT / 'font_8x16_pack'
        subprocess.run(['cc', '-O1', '-w', str(base / 'tools/font_8x16_pack.c'), '-o', str(packer)], check=True)
        packed.write_bytes(subprocess.run([str(packer)], capture_output=True, check=True).stdout)

    def edit(name, old, new):
        p = tree / name
        s = p.read_text(encoding='latin-1')
        if s.count(old) != 1:
            raise SystemExit(f'{name}: expected exactly one patch location')
        p.write_text(s.replace(old, new), encoding='latin-1')

    menu_edits(edit, palette=False)          # the palette swap is simulated in sim_chdk.c
    osd_edits(edit)                          # CHDK's shooting-screen OSD, as on the camera
    # CHDK keeps pointers in int fields (32-bit ARM); widen them for the host.
    edit('core/gui_menu.h', '    int                 text;       // Text', '    long                text;       // Text')
    edit('core/gui_menu.h', '    int                 title;', '    long                title;')
    edit('core/gui_menu.h', '(short)typ, (int)txt, (int*)val, (int)arg }', '(short)typ, (long)txt, (int*)val, (long)arg }')
    edit('core/gui_menu.h', '    int                 arg;        // additional argument', '    long                arg;        // additional argument')
    edit('include/lang.h', 'extern char* lang_str(int str);', 'extern char* lang_str(long str);')
    menu = (LAB / 'camera' / 'film_menu.c').read_text().replace('(int)"', '(long)"')
    (tree / 'core' / 'film_menu.c').write_text(menu)
    shutil.copy2(LAB / 'camera' / 'film_menu.h', tree / 'include' / 'film_menu.h')
    for n in ('film_lut.h', 'film_print.h', 'film_ui.h', 'film_queue.h', 'film_colour.h', 'film_gboy.h', 'film_gbui.h', 'film_riso.h', 'film_rsui.h', 'film_grade.h', 'film_grui.h'):
        shutil.copy2(LAB / 'camera' / n, tree / 'include' / n)

    objs = OUT / 'obj'
    objs.mkdir(parents=True, exist_ok=True)
    chdk_flags = ['-O1', '-g', '-w', '-fno-builtin', '-I' + str(tree / 'include'), '-I' + str(tree / 'include/std'),
                  '-I' + str(tree / 'core'), '-I' + str(tree / 'platform/g12'), '-I' + str(tree / 'platform/g12/sub/100g'),
                  '-I' + str(tree / 'modules'), '-I' + str(tree / 'lib/font'),
                  '-Dopen=sim_open', '-Dread=sim_read', '-Dlseek=sim_lseek', '-Dclose=sim_close']
    sim = LAB / 'tools' / 'menu_sim'
    # The simulator's "chdk-osd" key: CHDK r6355's own OSD defaults or Zrno's.
    osd_set = ['-DSIM_OSD_STOCK=' + ';'.join(f'conf.{n}={old}' for n, old, new in OSD_OFF),
               '-DSIM_OSD_ZRNO=' + ';'.join(f'conf.{n}={new}' for n, old, new in OSD_OFF)]
    jobs = [(tree / 'core/gui_menu.c', chdk_flags), (tree / 'core/gui_draw.c', chdk_flags),
            (tree / 'lib/font/rbf_font.c', chdk_flags), (tree / 'lib/font/font_8x16.c', chdk_flags), (tree / 'platform/g12/platform_palette.c', chdk_flags),
            (tree / 'core/film_menu.c', chdk_flags), (LAB / 'camera/film_ui.c', chdk_flags), (sim / 'sim_chdk.c', chdk_flags + osd_set),
            (LAB / 'camera/film_gbui.c', chdk_flags + ['-DFILM_GBUI_SYNC_LIVE']), (LAB / 'camera/film_rsui.c', chdk_flags), (LAB / 'camera/film_grui.c', chdk_flags)]
    # CHDK's shooting-screen OSD and its configuration defaults (the camera
    # side in sim_osd.c). The simulator keeps its own conf_save, which
    # writes nothing.
    jobs += [(tree / 'core' / (n + '.c'), chdk_flags) for n in ('gui_osd', 'gui_batt', 'gui_space', 'gui_usb')]
    jobs += [(tree / 'core/conf.c', chdk_flags + ['-DPLATFORMID=12815', '-Dconf_save=chdk_conf_save']), (sim / 'sim_osd.c', chdk_flags)]
    lut_flags = ['-O1', '-g', '-w', '-I' + str(LAB / 'tests/lut_stubs'), '-I' + str(LAB / 'tests/native_stubs'), '-I' + str(LAB / 'camera')]
    jobs += [(sim / 'sim_lut.c', lut_flags), (sim / 'sim_io.c', ['-O1', '-g', '-Wall'])]
    jobs += [(LAB / 'camera' / n, ['-O2']) for n in ('film_colour.c', 'film_retro.c', 'film_toy.c', 'film_print.c', 'film_grain.c', 'film_bloom.c')]
    jobs += [(LAB / 'camera' / n, ['-O2', '-I' + str(LAB / 'camera')]) for n in ('film_gboy.c', 'film_riso.c', 'film_grade.c')]
    outs = []
    for src, flags in jobs:
        o = objs / (src.parent.name + '_' + src.stem + '.o')
        r = subprocess.run(['cc', '-c', *flags, *SANITIZE, str(src), '-o', str(o)], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f'{src.name}:\n{r.stderr[-4000:]}')
        outs.append(str(o))
    r = subprocess.run(['cc', *SANITIZE, *outs, '-o', str(BIN)], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError('link:\n' + r.stderr[-4000:])


def stale():
    return not BIN.exists() or any(p.stat().st_mtime > BIN.stat().st_mtime for p in SOURCES if p.exists())


def card_dir(font):
    """A working directory holding a copy of the card's A/CHDK/FILM."""
    work = Path(tempfile.mkdtemp(prefix='menu-sim-'))
    film = work / 'A' / 'CHDK' / 'FILM'
    shutil.copytree(LAB / 'card' / 'CHDK' / 'FILM', film)
    sym = chdk_base() / 'CHDK' / 'SYMBOLS'
    shutil.copytree(sym, work / 'A' / 'CHDK' / 'SYMBOLS')
    if font:
        shutil.copy2(chdk_base() / 'CHDK' / 'FONTS' / (font + '.rbf'), film / 'MENU.RBF')
    return work


class Sim:
    def __init__(self, font=None, root=False):
        self.font, self.root, self.keys = font, root, []
        self.proc = None
        self.start()

    def start(self):
        if self.proc:
            self.proc.kill()
        self.work = card_dir(self.font)
        self.proc = subprocess.Popen([str(BIN)] + (['--root'] if self.root else []), cwd=self.work,
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.state, self.idx, self.pal = self.read()
        for k in self.keys:
            self.send(k, record=False)

    def read(self):
        head = self.proc.stdout.readline().decode().strip()
        idx = self.proc.stdout.read(W * H)
        pal = self.proc.stdout.read(1024)
        if len(idx) != W * H or len(pal) != 1024:
            raise RuntimeError('simulator stopped')
        return head, idx, pal

    def send(self, key, record=True):
        self.proc.stdin.write((key + '\n').encode())
        self.proc.stdin.flush()
        self.state, self.idx, self.pal = self.read()
        if record and self.state.startswith('OK'):
            self.keys.append(key)

    def png(self):
        return png(self.idx, self.pal)


def scene():
    """A stand-in for the live view behind transparent pixels."""
    rows = []
    for y in range(H):
        t = y / H
        sky = (int(90 + 80 * t), int(120 + 60 * t), int(160 + 30 * t)) if y < 140 else (int(70 - 20 * t), int(85 - 20 * t), 50)
        rows.append(sky)
    return rows


# Theme previews: (background, card, accent, secondary text) put into the
# film palette slots of the rendered frame. "Camera" uses film_menu.h as built.
THEMES = {
    'Camera build': None,
    'A Midnight (orange/navy)': ('020F2B', '0B2556', 'F95F01', '93A7CF'),
    'B Navy (orange/navy)': ('03246B', '0E3A8C', 'F95F01', 'AFC2E8'),
    'C Ink (orange/navy)': ('0B0F1A', '03246B', 'F95F01', '8E9BB5'),
    'Amber': ('14161A', '2A2F37', 'F2A93B', '98A0AC'),
    'Darkroom safelight': ('120D0D', '2A1C1C', 'E5484D', 'A89494'),
    'Teal': ('0F1A1C', '1E3236', '2EC4B6', '8FA9AC'),
    'Green': ('101611', '213025', '4CC38A', '93A598'),
    'iOS-style blue': ('1C1C1E', '2C2C2E', '0A84FF', '8E8E93'),
    'Mono': ('111111', '2A2A2A', 'E8E8E8', '8A8A8A'),
}


def yuv_rgb(e):
    """As sim_chdk.c converts a G12 palette entry."""
    y, u, v = (e >> 16) & 255, (e >> 8) & 255, e & 255
    u, v = u - 256 if u > 127 else u, v - 256 if v > 127 else v
    t = lambda x: int(x)
    c = (y + t(1402 * v / 1000), y - t((344 * u + 714 * v) / 1000), y + t(1772 * u / 1000))
    return bytes(min(255, max(0, x)) for x in c)


def themed(pal, theme):
    """The palette with a preview theme in the four film slots, while the
    camera would have them loaded (the menu or overlay is showing)."""
    cols = THEMES.get(theme)
    base = 0xB3
    if not cols or pal[4 * (base + 1):4 * (base + 1) + 3] == yuv_rgb(0x326EA40):   # CHDK's dark red: not loaded
        return pal
    pal = bytearray(pal)
    for slot, c in zip((1, 2, 4, 5), cols):
        pal[4 * (base + slot):4 * (base + slot) + 4] = bytes(int(c[k:k + 2], 16) for k in (0, 2, 4)) + b'\xff'
    return bytes(pal)


def png(idx, pal):
    """The 360 x 240 screen as a 720 x 540 PNG (G12 pixels are narrower than tall)."""
    bg = scene()
    lut = [tuple(pal[4 * i:4 * i + 4]) for i in range(256)]
    raw = bytearray()
    lines = []
    for y in range(H):
        row = bytearray()
        b = bg[y]
        for x in range(W):
            r, g, bl, a = lut[idx[y * W + x]]
            if a < 255:
                r, g, bl = ((r * a + b[0] * (255 - a)) // 255, (g * a + b[1] * (255 - a)) // 255, (bl * a + b[2] * (255 - a)) // 255)
            row += bytes((r, g, bl, r, g, bl))
        lines.append(bytes(row))
    for yo in range(540):
        raw += b'\0' + lines[yo * H // 540]

    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 720, 540, 8, 2, 0, 0, 0)) + \
        chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b'')


PAGE = '''<!doctype html><meta charset="utf-8"><title>Zrno menu simulator</title>
<style>
body{margin:0;background:#0d0e10;color:#c9ced6;font:14px -apple-system,system-ui,sans-serif;display:flex;flex-direction:column;align-items:center;gap:14px;padding:24px}
#lcd{width:720px;height:540px;border-radius:10px;box-shadow:0 0 0 10px #222,0 10px 40px #000;image-rendering:pixelated}
.keys{display:flex;gap:6px;flex-wrap:wrap;justify-content:center}
button{background:#23262c;color:#e8eaee;border:1px solid #3a3f48;border-radius:8px;padding:7px 12px;font:inherit;cursor:pointer}
button:hover{background:#2f333b}
#state{color:#98a0ac}#err{white-space:pre-wrap;color:#ff8a7a;font:12px ui-monospace,monospace;max-width:900px}
kbd{background:#23262c;border:1px solid #3a3f48;border-radius:4px;padding:0 5px}
</style>
<img id="lcd" alt="camera screen">
<label>Theme <select id="theme"></select></label>
<div id="state"></div>
<div class="keys">
<button data-k="s">S</button><button data-k="menu">MENU</button><button data-k="up">&#9650;</button>
<button data-k="down">&#9660;</button><button data-k="left">&#9664;</button><button data-k="right">&#9654;</button>
<button data-k="set">SET</button><button data-k="half">Half-press</button><button data-k="gallery">Gallery</button><button data-k="record">Shooting mode</button><button data-k="shoot">Shooting screen</button><button data-k="pause">Paused reason</button><button data-k="develop">Develop</button><button data-k="flip">Flip screen</button>
<button data-k="fail">Fail</button><button data-k="redraw">Redraw</button><button data-k="chdk-osd">CHDK OSD</button>
<button data-k="reset">Reset</button></div>
<div><kbd>&#8593;</kbd><kbd>&#8595;</kbd><kbd>&#8592;</kbd><kbd>&#8594;</kbd> move and change, <kbd>Enter</kbd> SET,
<kbd>[</kbd><kbd>]</kbd> back dial, <kbd>M</kbd> MENU, <kbd>S</kbd> S button, <kbd>D</kbd> develop a shot step by step, <kbd>Esc</kbd> shooting screen (turn Processing On to see the indicator), <kbd>P</kbd> cycle paused reasons, <kbd>G</kbd> gallery (adds Re-develop photo), <kbd>F</kbd> flip the screen to face the lens, <kbd>O</kbd> CHDK's OSD with CHDK's stock defaults (or Zrno's again), <kbd>R</kbd> reset. Edits to camera/film_menu.c rebuild on the next press.</div>
<div id="err"></div>
<script>
async function press(k){
  const r=await fetch('/key?k='+encodeURIComponent(k));const j=await r.json();
  document.getElementById('err').textContent=j.error||'';
  if(j.png)document.getElementById('lcd').src='data:image/png;base64,'+j.png;
  document.getElementById('state').textContent=j.state||'';
}
document.querySelectorAll('button').forEach(b=>b.onclick=()=>press(b.dataset.k));
const map={ArrowUp:'up',ArrowDown:'down',ArrowLeft:'left',ArrowRight:'right',Enter:'set','[':'dial-left',']':'dial-right',m:'menu',s:'s',r:'reset',h:'half',d:'develop',Escape:'shoot',p:'pause',g:'gallery',f:'flip',o:'chdk-osd'};
addEventListener('keydown',e=>{if(e.target.tagName==='SELECT')return;const k=map[e.key];if(k){e.preventDefault();press(k)}});
const sel=document.getElementById('theme');
THEMES.forEach(t=>{const o=document.createElement('option');o.textContent=t;sel.append(o)});
sel.onchange=()=>{sel.blur();press('theme:'+sel.value)};
press('refresh');
</script>'''


def serve(args):
    lock = threading.Lock()
    holder = {}

    holder['theme'] = 'Camera build'

    def current(key):
        if key.startswith('theme:'):
            holder['theme'] = key[6:] if key[6:] in THEMES else 'Camera build'
            key = 'refresh'

        if stale():
            build()
            if 'sim' in holder:
                holder['sim'].start()          # replay the presses on the new build
        if 'sim' not in holder or key == 'reset':
            holder['sim'] = Sim(args.font, args.root)
        sim = holder['sim']
        if key not in ('refresh', 'reset'):
            sim.send(key)
        return sim

    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def do_GET(self):
            if self.path.startswith('/key'):
                key = urllib.parse.unquote(self.path.split('k=', 1)[-1])
                with lock:
                    try:
                        sim = current(key)
                        frame = png(sim.idx, themed(sim.pal, holder['theme']))
                        body = {'state': sim.state[3:], 'png': base64.b64encode(frame).decode()}
                        if not sim.state.startswith('OK'):
                            body['error'] = sim.state
                    except Exception as e:
                        holder.pop('sim', None)
                        body = {'error': str(e)}
                data, kind = json.dumps(body).encode(), 'application/json'
            elif self.path in ('/', '/index.html'):
                page = PAGE.replace('<script>', '<script>const THEMES=' + json.dumps(list(THEMES)) + ';', 1)
                data, kind = page.encode(), 'text/html; charset=utf-8'
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', kind)
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(data)

    print(f'Zrno menu simulator: http://localhost:{args.port}')
    http.server.ThreadingHTTPServer(('127.0.0.1', args.port), H).serve_forever()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=['serve', 'shot', 'build'])
    ap.add_argument('rest', nargs='*')
    ap.add_argument('--port', type=int, default=8766)
    ap.add_argument('--font')
    ap.add_argument('--root', action='store_true')
    args = ap.parse_intermixed_args()
    if args.command == 'serve':
        serve(args)
        return
    build() if args.command == 'build' or stale() else None
    if args.command == 'shot':
        if not args.rest:
            raise SystemExit('shot needs an output file')
        sim = Sim(args.font, args.root)
        for k in args.rest[1:]:
            sim.send(k)
            if not sim.state.startswith('OK'):
                raise SystemExit(sim.state)
        Path(args.rest[0]).write_bytes(sim.png())
        print(sim.state[3:])


if __name__ == '__main__':
    main()
