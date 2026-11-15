#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebuild the camera presets from the included, pinned source CLUTs (offline)."""
from pathlib import Path
import hashlib
import json
import shutil
from hald_pack import load_hald
from cube_pack import pack
from bake_warm import bake
from hw_table import write_tables

lab = Path(__file__).resolve().parents[1]
folder = lab / 'card/CHDK/FILM/LUTS'
folder.mkdir(parents=True, exist_ok=True)
selection = json.loads((lab / 'luts/rawtherapee/selection.json').read_text())
presets = [(item['id'], item['label']) for item in selection]
bake(lab)
shutil.copyfile(lab / 'luts/GOLD17.LUT', folder / 'GOLD17.LUT')
shutil.copyfile(lab / 'luts/GOLD17.LUT', lab / 'card/CHDK/FILM/LOOK.LUT')
provenance = json.loads((lab / 'luts/rawtherapee/provenance.json').read_text())
report = {}
for name, label in presets:
    source = lab / 'luts/rawtherapee' / (name + '.PNG')
    target = folder / (name + '.LUT')
    if hashlib.sha256(source.read_bytes()).hexdigest() != provenance['files'][source.name]['sha256']:
        raise ValueError('Pinned source checksum mismatch: ' + source.name)
    cube = load_hald(source)
    target.write_bytes(pack(cube, 17))
    report[target.name] = dict(label=label, source=source.name, lattice_size=17,
        bytes=target.stat().st_size, sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
        license='CC-BY-SA-4.0', changes=f'{cube[0]}^3 Hald resampled tetrahedrally to 17^3, uint16 RGB, G12 checksum; L-mode output replicated to RGB')
(lab / 'luts/rawtherapee/converted.json').write_text(json.dumps(report, indent=2) + '\n')
catalog = 'GOLD17.LUT|Warm 200\n' + ''.join(f'{name}.LUT|{label}\n' for name, label in presets)
(lab / 'card/CHDK/FILM/LUTS.TXT').write_text(catalog)
print(f'Packed Warm 200 and {len(presets)} attributed RawTherapee presets.')
write_tables()  # each look as a DIGIC hardware colour table (.HWT)
