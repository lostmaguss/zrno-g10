#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the Zrno firmware (DISKBOOT.BIN and CHDK's modules) into card/.

  1. a fresh copy of the unmodified CHDK tree goes to build/firmware/chdk;
  2. tools/integrate.py adds Zrno to it;
  3. CHDK's make builds it;
  4. DISKBOOT.BIN and CHDK/MODULES/*.flt are copied to card/, and their
     hashes are written to build/firmware/manifest.json for the installer.

--fw picks the G12 firmware to build for (default 100g, the one Zrno is
tested on). Another firmware's build uses CHDK's own port for it and goes to
build/firmware-g12-<fw>-untested/ (its card files in card/ there), never to
card/: it has not been tested on a camera. --fw all builds each in turn.

Usage: python3 tools/build_firmware.py [--chdk TREE] [--cross PREFIX] [--fw FW|all]
See docs/BUILDING.md.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys

LAB = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LAB / 'tools'))
import firmware_variants as fv


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def run(args, **kw):
    print('+ ' + ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kw)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--chdk', type=Path, default=LAB / 'build/chdk-r6355', help='unmodified CHDK release-1_6 r6355 tree')
    p.add_argument('--cross', default=os.environ.get('CROSS_COMPILE', 'arm-none-eabi-'),
                   help='cross compiler prefix (default $CROSS_COMPILE or arm-none-eabi-)')
    p.add_argument('--fw', default='100g',
                   help='G12 firmware: 100g (default, tested), another of CHDK\'s G12 ports (untested), or all')
    a = p.parse_args()

    if not (a.chdk / 'Makefile').is_file():
        sys.exit(f'No CHDK tree at {a.chdk}: run python3 tools/fetch_chdk.py first.')
    known = fv.firmwares(a.chdk)
    if a.fw != 'all' and a.fw not in known:
        sys.exit(f'--fw {a.fw}: CHDK has no G12 port for it (it has {", ".join(known)}, or all).')

    version = subprocess.run([a.cross + 'gcc', '--version'], capture_output=True, text=True).stdout.split('\n')[0]
    print('Compiler:', version)
    if '10.3.1' not in version:
        print('Warning: Zrno is tested with xPack GNU Arm Embedded GCC 10.3.1-2.3; '
              'another compiler may not fit the core in memory or may behave differently.')
    for fw in known if a.fw == 'all' else [a.fw]:
        build(a, fw)


def build(a, fw):
    out = fv.firmware_dir(LAB, fw)
    out.mkdir(parents=True, exist_ok=True)
    if not fv.tested(fw):
        print(f'Building for G12 firmware {fv.label(fw)}: UNTESTED, never run on a camera. Output: {out.relative_to(LAB)}/')
    tree = out / 'chdk'
    shutil.rmtree(tree, ignore_errors=True)
    shutil.copytree(a.chdk, tree, ignore=shutil.ignore_patterns('*.o', '*.a'))
    run([sys.executable, LAB / 'tools/integrate.py', tree, fw])
    run(['make', 'PLATFORM=g12', 'PLATFORMSUB=' + fw, 'CROSS_COMPILE=' + a.cross, 'DEF_SVN_REF=6355', 'fir'], cwd=tree)

    manifest = out / 'manifest.json'
    card = fv.card_dir(LAB, fw)
    history = []
    if fv.tested(fw):
        history = json.loads(manifest.read_text()).get('boot_history', []) if manifest.is_file() else []
        if (card / 'DISKBOOT.BIN').is_file() and sha(card / 'DISKBOOT.BIN') not in history:
            history.append(sha(card / 'DISKBOOT.BIN'))   # so the installer accepts your previous build
    card.mkdir(parents=True, exist_ok=True)
    # The build date CHDK puts in the firmware (__DATE__ and __TIME__), for
    # the source zip's build command (tools/package_release.py).
    epoch = os.environ.get('SOURCE_DATE_EPOCH')
    files = {}
    shutil.copy2(tree / 'bin/DISKBOOT.BIN', card / 'DISKBOOT.BIN')
    files['DISKBOOT.BIN'] = sha(card / 'DISKBOOT.BIN')
    modules = card / 'CHDK/MODULES'
    shutil.rmtree(modules, ignore_errors=True)
    modules.mkdir(parents=True)
    for flt in sorted((tree / 'CHDK/MODULES').glob('*.flt')):
        shutil.copy2(flt, modules / flt.name)
        files['CHDK/MODULES/' + flt.name] = sha(flt)
    if fv.tested(fw):
        manifest.write_text(json.dumps({'files': files, 'boot_history': history, 'source_date_epoch': epoch},
                                       indent=2) + '\n')
        print(f'Built DISKBOOT.BIN {files["DISKBOOT.BIN"][:16]}… and {len(files) - 1} modules into card/.')
        print('Next: python3 tools/install_card.py (dry run), then add --apply.')
    else:
        manifest.write_text(json.dumps({'firmware': fw, 'tested': False, 'files': files, 'source_date_epoch': epoch},
                                       indent=2) + '\n')
        print(f'Built DISKBOOT.BIN {files["DISKBOOT.BIN"][:16]}… and {len(files) - 1} modules for G12 '
              f'{fv.label(fw)} (UNTESTED) into {card.relative_to(LAB)}/.')
        print(f'Next: python3 tools/package_release.py --fw {fw}')


if __name__ == '__main__':
    main()
