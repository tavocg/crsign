#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render the existing SVG into a multi-resolution Windows ICO (PNG entries)."""
import argparse
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--converter', default='rsvg-convert')
    args = parser.parse_args()
    sizes = (16, 32, 48, 256)
    images = [subprocess.check_output([args.converter, '-w', str(size), '-h', str(size), str(args.source)])
              for size in sizes]
    offset = 6 + 16 * len(images)
    entries = []
    for size, image in zip(sizes, images):
        if image[:8] != b'\x89PNG\r\n\x1a\n':
            raise RuntimeError('Icon renderer did not produce PNG')
        entries.append(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(image), offset))
        offset += len(image)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(struct.pack('<HHH', 0, 1, len(images)) + b''.join(entries) + b''.join(images))


if __name__ == '__main__':
    main()
