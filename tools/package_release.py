#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Package a built Zrno for a GitHub release, into build/release/:

  zrno-VERSION-g12-100g.zip         the card files: DISKBOOT.BIN, CHDK's
                                    modules and Zrno's data; install.command,
                                    the macOS installer that sets up a card
                                    with them (installer/), and CARD.SHA256,
                                    its list of the card files; VERSION,
                                    INSTALL.TXT, COPYING and THIRD_PARTY.md
  zrno-VERSION-g12-100g-chdk-source.zip
                                    the CHDK source the binary is built from:
                                    release-1_6 r6355 with Zrno integrated
                                    (GPL: the source of every binary), with
                                    CHDK's empty folders, so make runs in it
  SHA256SUMS-g12-100g               hashes of both

Run tools/build_firmware.py first, with SOURCE_DATE_EPOCH set for a
reproducible build (docs/BUILDING.md). Neither zip holds Canon code beyond
what CHDK's own G12 port holds: Zrno's copy of the develop task is made on
the camera, from its own ROM (camera/film_reloc.c).

--fw FW packages the build_firmware.py --fw FW build of another G12 firmware
instead, which has not been tested on a camera. Its files are named the same
way, with "untested" after the firmware: zrno-VERSION-g12-FW-untested.zip,
zrno-VERSION-g12-FW-untested-chdk-source.zip and SHA256SUMS-g12-FW-untested;
its INSTALL.TXT and installer name the firmware and say it is untested.
--fw all packages every build there is, 1.00G included.

Usage: python3 tools/package_release.py [--fw FW|all]
"""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess, sys, tempfile, zipfile

LAB = Path(__file__).resolve().parents[1]
OUT = LAB / 'build/release'
sys.path.insert(0, str(LAB / 'tools'))
import firmware_variants as fv

# The card zip's INSTALL.TXT; {version}, {commit} and {tag} are filled in.
INSTALL = LAB / 'docs/INSTALL.txt'


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def git(*args):
    return subprocess.run(['git', *args], cwd=LAB, capture_output=True, text=True, check=True).stdout.strip()


# Files the zip marks executable.
EXECUTABLE = {'install.command'}


def write_zip(path, entries):
    """entries: (name in zip, bytes); a name ending in / is a folder. Fixed
    times and order, so a rebuild matches."""
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in sorted(entries):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3   # Unix, so unzip tools keep the mode
            info.external_attr = (0o100755 if name in EXECUTABLE else 0o100644) << 16
            if name.endswith('/'):
                info.compress_type = zipfile.ZIP_STORED
                info.external_attr = 0o40755 << 16 | 0x10   # a folder, for Unix and DOS
            z.writestr(info, data)


def replace_once(text, old, new, what):
    if text.count(old) != 1:
        sys.exit(f'{what}: expected exactly one "{old.strip()}" to mark as untested')
    return text.replace(old, new)


def untested_install_txt(text, fw, version):
    """docs/INSTALL.txt, filled in, for a build of untested firmware fw."""
    fwl = fv.label(fw)
    title, _rule, rest = text.split('\n', 2)
    title = replace_once(title, 'firmware 1.00G only', f'firmware {fwl} only, UNTESTED', 'INSTALL.txt')
    text = f'{title}\n{"=" * len(title)}\n{rest}'
    text = replace_once(text, 'own risk: there is no warranty (see COPYING).\n', (
        'own risk: there is no warranty (see COPYING).\n\n'
        f'UNTESTED: this build is for firmware {fwl}. It is built from CHDK\'s own\n'
        f'port for the G12 {fwl}, but Zrno has been tested only on firmware 1.00G,\n'
        'and this build has never run on a camera. It may not start, it may crash,\n'
        'or Zrno may leave the photos to Canon and show "unknown firmware". The\n'
        f'tested release is zrno-{version}-g12-100g.zip, for firmware 1.00G.\n'), 'INSTALL.txt')
    text = replace_once(text, 'Zrno runs only on firmware 1.00G.', f'This build needs firmware {fwl}.', 'INSTALL.txt')
    return replace_once(text, 'it must say GM1.00G.', f'it must say GM{fwl}.', 'INSTALL.txt')


def untested_installer(text, fw):
    """installer/install.command for a build of untested firmware fw."""
    fwl = fv.label(fw)
    text = replace_once(text, '# for the Canon PowerShot G12 (firmware 1.00G) and',
                        f'# for the Canon PowerShot G12 (firmware {fwl}, UNTESTED build) and', 'install.command')
    head = f'Zrno installer for the Canon PowerShot G12 (firmware {fwl}, UNTESTED)'
    old = "say 'Zrno installer for the Canon PowerShot G12 (firmware 1.00G)'\nsay '" + '=' * 59 + "'\n"
    return replace_once(text, old, (
        f"say '{head}'\nsay '{'=' * len(head)}'\n"
        f"say 'This build for firmware {fwl} has never run on a camera: Zrno is tested on 1.00G only.'\n"), 'install.command')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--fw', default='100g', help='G12 firmware build to package: 100g (default), another, or all')
    a = p.parse_args()
    if not (LAB / 'build/chdk-r6355/Makefile').is_file():
        sys.exit('No CHDK tree: run tools/fetch_chdk.py first.')
    if a.fw == 'all':
        fws = [fw for fw in fv.firmwares(LAB / 'build/chdk-r6355') if (fv.firmware_dir(LAB, fw) / 'manifest.json').is_file()]
        if not fws:
            sys.exit('No firmware built yet: run tools/build_firmware.py first.')
    elif a.fw in fv.firmwares(LAB / 'build/chdk-r6355'):
        fws = [a.fw]
    else:
        sys.exit(f'--fw {a.fw}: not a G12 firmware CHDK has a port for (or all).')
    for fw in fws:
        package(fw)


def package(fw):
    manifest = json.loads((LAB / 'card-manifest.json').read_text())
    built = fv.firmware_dir(LAB, fw) / 'manifest.json'
    if not built.is_file():
        sys.exit(f'No firmware built for {fv.label(fw)} yet: run tools/build_firmware.py'
                 + ('' if fv.tested(fw) else f' --fw {fw}') + ' first.')
    data = dict(manifest['files'])
    built = json.loads(built.read_text())
    firmware = built['files']
    epoch = built.get('source_date_epoch')
    if not epoch:
        print('Warning: built without SOURCE_DATE_EPOCH: the source zip cannot rebuild this exact DISKBOOT.BIN')
    files = dict(data, **firmware)
    def source(name):
        return (fv.card_dir(LAB, fw) if name in firmware else LAB / 'card') / name
    for name, h in files.items():
        if sha(source(name)) != h:
            sys.exit(f'{source(name).relative_to(LAB)} differs from its manifest hash: rebuild before packaging.')
    version = manifest['version']
    commit = git('rev-parse', '--short', 'HEAD')
    if git('status', '--porcelain', '--untracked-files=no'):
        print('Warning: the working tree has uncommitted changes; the release will not match', commit)

    OUT.mkdir(parents=True, exist_ok=True)
    name_stem = f'zrno-{version}-{fv.tag(fw)}'
    card_zip = OUT / f'{name_stem}.zip'
    entries = [(name, source(name).read_bytes()) for name in files]
    card_sums = ''.join(f'{h}  {name}\n' for name, h in sorted(files.items()))
    installer = (LAB / 'installer/install.command').read_bytes()
    install_txt = INSTALL.read_text().format(version=version, commit=commit, tag=fv.tag(fw))
    if not fv.tested(fw):
        installer = untested_installer(installer.decode(), fw).encode()
        install_txt = untested_install_txt(install_txt, fw, version)
    entries += [('CARD.SHA256', card_sums.encode()),
                ('VERSION', f'{version}\n'.encode()),
                ('install.command', installer)]
    entries += [('INSTALL.TXT', install_txt.replace('\n', '\r\n').encode()),
                ('COPYING', (LAB / 'COPYING').read_bytes()),
                ('THIRD_PARTY.md', (LAB / 'THIRD_PARTY.md').read_bytes())]
    write_zip(card_zip, entries)

    # The corresponding source: the pinned CHDK tree with Zrno integrated, as
    # tools/build_firmware.py builds it, without build products.
    src_zip = OUT / f'{name_stem}-chdk-source.zip'
    chdk = LAB / 'build/chdk-r6355'
    if not (chdk / 'Makefile').is_file():
        sys.exit('No CHDK tree: run tools/fetch_chdk.py first.')
    with tempfile.TemporaryDirectory() as tmp:
        tree = Path(tmp) / 'chdk'
        # Exactly the pinned files (tools/chdk-r6355.sha256), checked again.
        for line in (LAB / 'tools/chdk-r6355.sha256').read_text().splitlines():
            h, name = line.split('  ', 1)
            if sha(chdk / name) != h:
                sys.exit(f'{chdk / name} differs from tools/chdk-r6355.sha256: fetch CHDK again.')
            (tree / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(chdk / name, tree / name)
        # And CHDK's empty folders (bin/ and CHDK/MODULES/), which its make
        # writes into but does not create.
        for d in chdk.rglob('*'):
            if d.is_dir() and not any(d.iterdir()):
                (tree / d.relative_to(chdk)).mkdir(parents=True, exist_ok=True)
        subprocess.run([sys.executable, LAB / 'tools/integrate.py', tree, fw], check=True, capture_output=True)
        prefix = src_zip.name[:-len('.zip')] + '/'
        entries = [(prefix + p.relative_to(tree).as_posix(), p.read_bytes()) for p in tree.rglob('*') if p.is_file()]
        entries += [(prefix + p.relative_to(tree).as_posix() + '/', b'') for p in tree.rglob('*')
                    if p.is_dir() and not any(p.iterdir())]
        make = f'make PLATFORM=g12 PLATFORMSUB={fw} CROSS_COMPILE=<xPack GCC 10.3.1-2.3 prefix> DEF_SVN_REF=6355 fir'
        build_txt = (f'CHDK release-1_6 r6355 with Zrno {version} ({commit}) integrated by tools/integrate.py.\n'
                     + (f'Build: SOURCE_DATE_EPOCH={epoch} {make}\n'
                        'The build date is the release\'s, so bin/DISKBOOT.BIN and CHDK/MODULES/ match its card files.\n'
                        if epoch else f'Build: {make}\n'))
        if not fv.tested(fw):
            build_txt = (f'UNTESTED: for G12 firmware {fv.label(fw)} (tools/integrate.py TREE {fw}); '
                         'Zrno is tested on firmware 1.00G only.\n' + build_txt)
        entries.append((prefix + 'ZRNO-BUILD.TXT', build_txt.encode()))
        write_zip(src_zip, entries)

    sums = ''.join(f'{sha(p)}  {p.name}\n' for p in (card_zip, src_zip))
    (OUT / f'SHA256SUMS-{fv.tag(fw)}').write_text(sums)
    print(sums, end='')
    print(f'{len(files)} card files; release files in {OUT.relative_to(LAB)}/')


if __name__ == '__main__':
    main()
