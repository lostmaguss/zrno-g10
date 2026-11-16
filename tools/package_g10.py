#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build and package Magus (Zrno for the G10 1.03B) for a GitHub release,
into build/release-g10/:

  magus-VERSION-g10-103b.zip          the card files: DISKBOOT.BIN, CHDK's
                                      data folders and the modules built with
                                      it, Magus's looks (CHDK/MAGUS); INSTALL.TXT,
                                      mark_boot_g10.py, VERSION, CARD.SHA256,
                                      COPYING, THIRD_PARTY.md
  magus-VERSION-g10-103b-chdk-source.zip
                                      the source the binary is built from:
                                      CHDK release-1_6 r6355 with Magus
                                      integrated (GPL), and MAGUS-BUILD.TXT
  SHA256SUMS-g10-103b                 hashes of both

The firmware is built here from a fresh copy of build/chdk-r6355 (fetch it
with ZRNO_EXTRA_PORTS=g10 tools/fetch_chdk.py), with SOURCE_DATE_EPOCH set
to the commit's time, so the build is reproducible from the source zip.

Usage: python3 tools/package_g10.py VERSION [--make MAKE] [--cross PREFIX]
"""
from pathlib import Path
import argparse, hashlib, os, shutil, subprocess, sys, zipfile

LAB = Path(__file__).resolve().parents[1]
OUT = LAB / 'build/release-g10'
MAKE_ARGS = ['PLATFORM=g10', 'PLATFORMSUB=103b', 'DEF_SVN_REF=6355', 'fir']
CARD_DATA = ['BOOKS', 'CURVES', 'DATA', 'FONTS', 'GAMES', 'GRIDS', 'HELP', 'LANG', 'LUALIB', 'SCRIPTS', 'SYMBOLS',
             'logo.dat', 'logo_de.dat', 'syscurves.CVF']

INSTALL = """Magus {version} - Zrno for the Canon PowerShot G10, firmware 1.03B
Built from commit {commit} of https://github.com/lostmaguss/zrno-g10

WARNING: unofficial firmware, not made, supported or endorsed by Canon or by
Zrno's author. It changes how the camera develops photos. It can crash the
camera or lose photos, and in the worst case damage it. Use it at your own
risk; there is no warranty (see COPYING). Tested on one G10 with firmware
1.03B. Use it ONLY on a G10 with firmware 1.03B.

1. Check the firmware
   Put an empty file named ver.req on a card. Switch the camera on in
   playback and press FUNC/SET + DISP. It must say GM1.03B.

2. Set up the card (this erases it: back it up first)
   The G10 starts CHDK from a card only from a small FAT16 partition marked
   bootable, so the card gets two partitions. On Windows, in a command
   prompt run as administrator:

     diskpart
     list disk
     select disk N            (N = your SD card: check the size!)
     clean
     create partition primary size=32
     format fs=fat quick label=ZRNO_BOOT
     set id=06
     assign
     create partition primary
     format fs=fat32 quick label=ZRNO
     assign
     exit

   Then mark the ZRNO_BOOT partition bootable (Python 3, as administrator;
   E = its drive letter):

     python mark_boot_g10.py E

   It refuses any volume that is not a 20-40 MB FAT16 one labelled
   ZRNO_BOOT, and saves the old boot sector first. (CHDK's own tools, such as
   SDMinste or STICK, can also mark a card bootable.)

3. Copy the files
   - to ZRNO_BOOT: DISKBOOT.BIN
   - to ZRNO: DISKBOOT.BIN and the CHDK folder
   CARD.SHA256 lists every card file with its SHA-256 hash.

4. Start it
   Lock the card (slider down), put it in the camera, switch it on. The blue
   Print light flashes: CHDK and Magus are running. With the card unlocked
   the camera starts as a normal Canon. Nothing is installed in the camera.

5. Use it
   Press Print (ALT mode), then MENU, and open Magus: Processing, Film look,
   Halation, Halation colour, Bloom, Grain, Last shot. Shoot in P, Tv, Av or
   M, image size L, My Colors Off. A photo with effects takes a few seconds
   to save (up to about 20 s with everything at High on a bright scene);
   Last shot shows the time, or why a photo was skipped.

More: https://github.com/lostmaguss/zrno-g10 (README and docs/PORT-G10.md).
Magus is a port of Zrno by suzvuk (https://github.com/suzvuk/zrno). Free
software under the GNU GPL, version 2 or later (COPYING); the
RawTherapee-derived looks are CC BY-SA 4.0 (THIRD_PARTY.md). The source of
this firmware is in magus-{version}-g10-103b-chdk-source.zip.
"""

BUILD_TXT = """Magus {version}: CHDK release-1_6 r6355 with Magus integrated
(tools/integrate_g10.py --magus at commit {commit} of
https://github.com/lostmaguss/zrno-g10). This is the source of DISKBOOT.BIN
and the modules in magus-{version}-g10-103b.zip.

Build (xPack GNU Arm Embedded GCC 10.3.1-2.3):

  SOURCE_DATE_EPOCH={epoch} make PLATFORM=g10 PLATFORMSUB=103b \\
      CROSS_COMPILE=arm-none-eabi- DEF_SVN_REF=6355 fir

The firmware is bin/DISKBOOT.BIN, the modules CHDK/MODULES.
"""


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args, **kw):
    print('+ ' + ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kw)


def zip_tree(zf, folder, prefix):
    for p in sorted(folder.rglob('*')):
        name = (prefix + '/' + p.relative_to(folder).as_posix()).lstrip('/')
        if p.is_dir():
            if not any(p.iterdir()):
                zf.writestr(name + '/', '')              # keep empty folders: make needs some
        else:
            zf.write(p, name)


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument('version')
    a.add_argument('--make', default=os.environ.get('MAKE', 'make'))
    a.add_argument('--cross', default=os.environ.get('CROSS_COMPILE', 'arm-none-eabi-'))
    o = a.parse_args()
    chdk = LAB / 'build/chdk-r6355'
    if not (chdk / 'platform/g10/sub/103b').is_dir():
        sys.exit('No CHDK tree with the G10 port: run ZRNO_EXTRA_PORTS=g10 python3 tools/fetch_chdk.py')
    if subprocess.run(['git', 'status', '--porcelain'], cwd=LAB, capture_output=True, text=True).stdout.strip():
        sys.exit('Commit your changes first: the release names the commit it is built from.')
    commit = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=LAB, capture_output=True, text=True).stdout.strip()
    epoch = subprocess.run(['git', 'log', '-1', '--format=%ct'], cwd=LAB, capture_output=True, text=True).stdout.strip()
    stem = f'magus-{o.version}-g10-103b'
    shutil.rmtree(OUT, ignore_errors=True)
    src, tree = OUT / 'src', OUT / 'tree'

    # The source: a fresh copy with Magus integrated, before anything is built.
    shutil.copytree(chdk, src, ignore=shutil.ignore_patterns('*.o', '*.a'))
    run([sys.executable, LAB / 'tools/integrate_g10.py', src, '--magus'])
    (src / 'MAGUS-BUILD.TXT').write_text(BUILD_TXT.format(version=o.version, commit=commit, epoch=epoch), newline='\n')

    # The binary, from a copy of that source.
    shutil.copytree(src, tree)
    env = dict(os.environ, SOURCE_DATE_EPOCH=epoch)
    run([o.make, 'CROSS_COMPILE=' + o.cross] + MAKE_ARGS, cwd=tree, env=env)

    # The card.
    card = OUT / 'card'
    (card / 'CHDK').mkdir(parents=True)
    shutil.copy2(tree / 'bin/DISKBOOT.BIN', card / 'DISKBOOT.BIN')
    for name in CARD_DATA:
        p = src / 'CHDK' / name
        if p.is_dir():
            shutil.copytree(p, card / 'CHDK' / name, ignore=shutil.ignore_patterns('Makefile', '*.o'))
        elif p.is_file():
            shutil.copy2(p, card / 'CHDK' / name)
    shutil.copytree(tree / 'CHDK/MODULES', card / 'CHDK/MODULES', ignore=shutil.ignore_patterns('Makefile', '*.o', '*.c', '*.h'))
    shutil.copytree(LAB / 'card-g10/CHDK/MAGUS', card / 'CHDK/MAGUS')
    files = sorted(p for p in card.rglob('*') if p.is_file())
    card_sha = ''.join(f'{sha(p)}  {p.relative_to(card).as_posix()}\n' for p in files)

    zip_path = OUT / f'{stem}.zip'
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        zip_tree(zf, card, '')
        zf.writestr('INSTALL.TXT', INSTALL.format(version=o.version, commit=commit).replace('\n', '\r\n'))
        zf.writestr('VERSION', f'{o.version} {commit}\n')
        zf.writestr('CARD.SHA256', card_sha)
        zf.write(LAB / 'tools/mark_boot_g10.py', 'mark_boot_g10.py')
        zf.write(LAB / 'COPYING', 'COPYING')
        zf.write(LAB / 'THIRD_PARTY.md', 'THIRD_PARTY.md')
    src_zip = OUT / f'{stem}-chdk-source.zip'
    with zipfile.ZipFile(src_zip, 'w', zipfile.ZIP_DEFLATED) as zf:
        zip_tree(zf, src, f'{stem}-chdk-source')
    sums = OUT / 'SHA256SUMS-g10-103b'
    sums.write_text(''.join(f'{sha(p)}  {p.name}\n' for p in (zip_path, src_zip)), newline='\n')
    print(f'{zip_path.name}: {zip_path.stat().st_size} bytes, {len(files)} card files')
    print(f'{src_zip.name}: {src_zip.stat().st_size} bytes')
    print('DISKBOOT.BIN', sha(card / 'DISKBOOT.BIN'))


if __name__ == '__main__':
    main()
