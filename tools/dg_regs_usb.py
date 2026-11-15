#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Take one shot over USB and save the develop's register and table state.

Runs Lua SETUP on the camera (for example set_prop calls), shoots, waits WAIT
seconds for the develop and Zrno to finish, then reads the register
shadow, ManageImgTbl's state, the parameter block and the three table buffers
with chdkptp's rmem. Writes a DGR1 snapshot that tools/dg_regs_diff.py reads,
the same layout as tools/probes/dg_regs_probe.c. No module or card swap needed.

  python3 tools/dg_regs_usb.py OUT.BIN 'set_prop(188,11) set_prop(229,2)' [WAIT]

The buffers hold the last values Canon built, which live view rebuilds after
the shot for the current settings; the register shadow is partly rewritten by
live view, so trust table differences more than register differences. Check
that the shot happened: param word 81 is the file number as ASCII.
"""
import sys, struct, time, os, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g12_usb as U
out, setup = sys.argv[1], sys.argv[2]; wait = float(sys.argv[3]) if len(sys.argv) > 3 else 25
tmp = tempfile.mkdtemp(dir=os.path.dirname(os.path.abspath(out)))
t0 = time.time()
U.chdk(f'luar {setup} sleep(500) shoot() sleep({int(wait*1000)}) return get_tick_count()', f'rmem 0x6748 0x400 -f={tmp}/state')
st = open(f'{tmp}/state', 'rb').read()
angel, lucky, base = struct.unpack_from('<I', st, 4)[0], struct.unpack_from('<I', st, 8)[0], struct.unpack_from('<I', st, 16)[0]
def ok(p, n): a = p & ~0x40000000; return p and not p & 3 and 0x1000 <= a and a + n <= 0x8000000
secs = [('shadow', 0x400000, 0x40000), ('state', 0x6748, 0x400), ('param', 0xb3e00, 0x300),
        ('lucky', lucky, 0x1400), ('angel', angel, 0x400), ('base', base, 0x800)]
cmds = [f'rmem {a:#x} {n:#x} -f={tmp}/{nm}' for nm, a, n in secs if nm != 'state' and ok(a, n)]
U.chdk(*cmds)
h = [0] * 32
h[0], h[1], h[3] = 0x31524744, 1, int(time.time() * 1000) & 0xffffffff
for i, (nm, a, n) in enumerate(secs): h[4 + 2*i], h[5 + 2*i] = a, n
h[16] = ok(lucky, 0x1400) | ok(angel, 0x400) << 1 | ok(base, 0x800) << 2
body = b''
for nm, a, n in secs:
    p = f'{tmp}/{nm}'
    body += open(p, 'rb').read() if os.path.exists(p) else bytes(n)
open(out, 'wb').write(struct.pack('<32I', *h) + body)
print(f'{out}: lucky={lucky:#x} angel={angel:#x} base={base:#x} ok={h[16]} in {time.time()-t0:.0f}s')
