#!/usr/bin/env python3
"""Generate Ari's small nine-slice UI assets without external dependencies.

These are geometric theme primitives, not illustration assets. Four samples per
axis provide antialiased curves. Corners stay fixed when Fcitx stretches them.
"""
import math
from pathlib import Path
import struct
import zlib


def rounded_png(path, size, radius, fill, border, padding=0):
    def inside(x, y, inset):
        edge = padding + inset
        r = max(0, radius - inset)
        cx = min(max(x, edge + r), size - edge - r)
        cy = min(max(y, edge + r), size - edge - r)
        return (edge <= x <= size - edge and edge <= y <= size - edge
                and math.hypot(x - cx, y - cy) <= r)

    pixels = bytearray()
    for y in range(size):
        pixels.append(0)  # PNG filter: none
        for x in range(size):
            samples = []
            for sy in range(4):
                for sx in range(4):
                    px, py = x + (sx + .5) / 4, y + (sy + .5) / 4
                    if inside(px, py, 0):
                        samples.append(fill if inside(px, py, 1) else border)
            if samples:
                pixels.extend(round(sum(c[i] for c in samples) / len(samples))
                              for i in range(3))
                pixels.append(round(255 * len(samples) / 16))
            else:
                pixels.extend((0, 0, 0, 0))

    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data
                + struct.pack('>I', zlib.crc32(kind + data)))

    path.write_bytes(b'\x89PNG\r\n\x1a\n'
                     + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(pixels, 9)) + chunk(b'IEND', b''))


def main():
    themes = Path(__file__).resolve().parents[1] / 'data' / 'themes'
    for name, background, border, selection in (
        ('ari-dark', (32, 34, 39), (65, 69, 78), (42, 65, 95)),
        ('ari-light', (250, 251, 253), (205, 211, 222), (225, 236, 255)),
    ):
        path = themes / name
        path.mkdir(parents=True, exist_ok=True)
        rounded_png(path / 'panel.png', 72, 12, background, border, padding=4)
        rounded_png(path / 'selection.png', 32, 6, selection, selection)


if __name__ == '__main__':
    main()
