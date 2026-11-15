# SPDX-License-Identifier: GPL-2.0-or-later
"""The G12 firmware versions Zrno builds for, and where each build goes.

CHDK r6355 ships a G12 port for each firmware it knows (platform/g12/sub/:
100c, 100e, 100f, 100g). Zrno is tested on 1.00G only. A build for another
firmware uses CHDK's own port for it, so its addresses are that firmware's,
and CHDK's loader still starts it only on that firmware. Its files name the
firmware and say "untested".
"""
from pathlib import Path

TESTED = ('100g',)


def firmwares(chdk):
    """The G12 firmware sub-ports in an unmodified CHDK tree, e.g. ['100c', ...]."""
    return sorted(p.name for p in (Path(chdk) / 'platform/g12/sub').iterdir() if (p / 'makefile.inc').is_file())


def tested(fw):
    return fw in TESTED


def label(fw):
    """'100f' -> '1.00F', as the camera shows it (GM1.00F)."""
    return f'{fw[0]}.{fw[1:3]}{fw[3:].upper()}'


def tag(fw):
    """'100g' -> 'g12-100g'; '100f' -> 'g12-100f-untested'."""
    return f'g12-{fw}' + ('' if tested(fw) else '-untested')


def firmware_dir(lab, fw):
    """Where build_firmware.py puts a build's CHDK tree and manifest.json."""
    return Path(lab) / ('build/firmware' if tested(fw) else f'build/firmware-{tag(fw)}')


def card_dir(lab, fw):
    """Where a build's DISKBOOT.BIN and CHDK/MODULES go: card/ for the tested
    firmware (with Zrno's data files, for tools/install_card.py), the build's
    own folder for the others."""
    return Path(lab) / 'card' if tested(fw) else firmware_dir(lab, fw) / 'card'
