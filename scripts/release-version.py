#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Map release tags to the numeric version required by Windows Installer."""
import argparse
import re


def version(tag, build):
    match = re.fullmatch(r'v?(\d+)\.(\d+)\.(\d+)(?:[-+].*)?', tag)
    values = tuple(map(int, match.groups())) if match else (0, 0, int(build))
    if any(n > limit or n < 0 for n, limit in zip(values, (255, 255, 65535))):
        raise ValueError('Version exceeds Windows Installer limits')
    return '.'.join(map(str, values))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--build', required=True)
    args = parser.parse_args()
    print(version(args.tag, args.build))
