#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pack an sRGB HaldCLUT PNG as a checked G12 17/33-point LUT.

Pixel values are used directly, without assigning/converting colour profiles.
Requires Pillow. The source must be a genuine RGB Hald, red axis fastest.
"""
import argparse
from pathlib import Path
from PIL import Image
from cube_pack import pack

class _HaldValues:
    """Read only the sampled RGB nodes; avoid millions of Python tuples."""
    def __init__(self, pixels):
        self.pixels = pixels
    def __len__(self):
        return len(self.pixels) // 3
    def __getitem__(self, index):
        if index < 0: index += len(self)
        if not 0 <= index < len(self): raise IndexError(index)
        offset = index * 3
        return tuple(v / 255 for v in self.pixels[offset:offset+3])


def load_hald(path):
    with Image.open(path) as image:
        if image.width != image.height or image.mode not in ('RGB', 'L'):
            raise ValueError('Expected a square RGB or monochrome HaldCLUT')
        level = round(image.width ** (1 / 3))
        if level ** 3 != image.width or not 2 <= level <= 16:
            raise ValueError('Invalid Hald level (expected 2..16)')
        size = level * level
        # L-mode CLUTs encode monochrome output; expanding channels changes
        # no stored tone values and performs no colour-profile conversion.
        if image.mode == 'L': image = image.convert('RGB')
        values = _HaldValues(image.tobytes())
    return size, [0., 0., 0.], [1., 1., 1.], values

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input'); p.add_argument('output')
    p.add_argument('--size', type=int, choices=[17, 33], default=17)
    args = p.parse_args()
    Path(args.output).write_bytes(pack(load_hald(args.input), args.size))
