#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read Canon's develop settings record over USB (probes R23 and R24).

The settings record allocator (0xff895680) copies eight byte ranges of the
live settings record, whose pointer is at 0x31f8 + 8 (context: [0] init,
[4] pool, [8] live record, [12] pool slots of 0xdb8 bytes), into a free pool
slot for each capture. This reads it without calling any Canon code.

  python3 tools/dg_rec_usb.py live OUT_DIR [LUA_SETUP]
      Run LUA_SETUP (e.g. 'set_prop(272,3)'), wait 1 s, then save ctx.bin,
      live.bin (0xdb8), pool.bin (all slots) and props.txt (get_prop 0..999).
  python3 tools/dg_rec_usb.py shot OUT_DIR [LUA_SETUP]
      Shooting mode: run LUA_SETUP, take one photo with press('shoot_full'),
      and copy the shot's record (shot record + 0x18) every ~10 ms while it
      is in use; save the fullest of the last copies as shot.bin (the record as the capture
      leaves it, like R22a's .SET), plus the live record before and after.
"""
import os, re, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g12_usb as U

CTX, REC = 0x31f8, 0xdb8
RANGES = [(0x8, 0x161), (0x169, 0x584), (0x6ed, 0x280), (0x96d, 0x9c), (0xa09, 0xd4), (0xadd, 0x1b8), (0xd27, 0x40), (0xd68, 0x50)]


def ram(a): return 0x1000 <= a < 0x800000


def returned(out):
    m = re.search(r"return:'([0-9a-f]*)'", out) or re.search(r'return:"([0-9a-f]*)"', out)
    if not m: sys.exit('no data returned:\n' + out)
    return bytes.fromhex(m.group(1))


def rmem(out_dir, name, addr, n):
    path = os.path.join(out_dir, name)
    U.chdk(f'rmem {addr:#x} {n:#x} -f={path}', quiet=True)
    return open(path, 'rb').read()


def ctx(out_dir):
    c = struct.unpack('<4I', rmem(out_dir, 'ctx.bin', CTX, 16))
    print(f'ctx {CTX:#x}: init={c[0]:#x} pool={c[1]:#x} live={c[2]:#x} slots={c[3]}')
    if not (ram(c[1]) and ram(c[2]) and 0 < c[3] < 64): sys.exit('context does not look like the settings records')
    return c


PROPS_LUA = ("local t={} for i=0,999 do local v=get_prop(i) t[#t+1]=string.format('%d=%d',i,v or -99999) end "
             "return table.concat(t,' ')")


def live(out_dir, setup):
    if setup: U.chdk(f'luar {setup} sleep(1000)')
    c = ctx(out_dir)
    rmem(out_dir, 'live.bin', c[2], REC)
    rmem(out_dir, 'pool.bin', c[1], c[3] * REC)
    out = U.chdk(f'luar {PROPS_LUA}', quiet=True)
    m = re.search(r"return:'([^']*)'", out)
    open(os.path.join(out_dir, 'props.txt'), 'w').write((m.group(1) if m else out).replace(' ', '\n') + '\n')
    print(f'{out_dir}: live.bin, pool.bin ({c[3]} slots), props.txt')


SHOT_LUA = """
local live=peek(0x31f8+8)
local keep,seen,n={},false,0
press('shoot_full')
local t0=get_tick_count()
while get_tick_count()-t0 < 20000 do
  local s=peek(0x76d8+16)
  if s>0x1000 and s<0x800000 then
    local h=peek(s+0x18)
    if h>0x1000 and h<0x800000 and h~=live and peek(h+4)==1 then
      local w={} for i=0,877 do w[i+1]=string.format('%08x',peek(h+i*4)) end
      keep[#keep+1]=table.concat(w) if #keep>4 then table.remove(keep,1) end seen=true n=n+1
    elseif seen then break end
  end
  sleep(10)
end
release('shoot_full')
sleep(1500)
return string.format('%08x%08x%08x',n,get_tick_count()-t0,#keep)..table.concat(keep)
"""


def shot(out_dir, setup):
    c = ctx(out_dir)
    rmem(out_dir, 'live-before.bin', c[2], REC)
    if setup: U.chdk(f'luar {setup} sleep(1000)')
    lua = ' '.join(l.strip() for l in SHOT_LUA.strip().splitlines())
    data = returned(U.chdk(f'luar {lua}', quiet=True))
    n, ms, k = struct.unpack('>III', data[:12])
    copies = []
    for j in range(k):                                # Lua sends each copy's words big-endian
        words = data[12 + j * REC:12 + (j + 1) * REC]
        copies.append(b''.join(struct.pack('<I', int.from_bytes(words[i:i + 4], 'big')) for i in range(0, len(words), 4)))
    # The free (0xff8958b8) wipes the data before clearing "in use": keep the fullest copy.
    rec = max(copies, key=lambda r: sum(1 for x in r[8:] if x)) if copies else b''
    print('copies kept:', [sum(1 for x in r[8:] if x) for r in copies], 'nonzero bytes')
    rmem(out_dir, 'live-after.bin', c[2], REC)
    if len(rec) != REC: sys.exit(f'no shot record caught ({n} copies in {ms} ms): did the shot fire?')
    open(os.path.join(out_dir, 'shot.bin'), 'wb').write(rec)
    print(f'{out_dir}: shot.bin from {n} copies over {ms} ms; live-before.bin, live-after.bin')


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ('live', 'shot'): sys.exit(__doc__)
    out_dir, setup = sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else ''
    os.makedirs(out_dir, exist_ok=True)
    (live if sys.argv[1] == 'live' else shot)(out_dir, setup)


if __name__ == '__main__':
    main()
