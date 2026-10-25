#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Missing OpenSC must fail by default and be genuinely optional when disabled."""
from pathlib import Path
import subprocess
import sys
import tempfile

cmake, module = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='crsign-vendor-') as temporary:
    root = Path(temporary)
    (root / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.16)\nproject(vendor_check NONE)\n'
        f'include([[{Path(module).resolve().as_posix()}]])\n', encoding='utf-8')
    default = subprocess.run([cmake, '-S', str(root), '-B', str(root / 'default')],
                             capture_output=True, text=True, timeout=30)
    assert default.returncode != 0, default.stdout
    assert 'Missing vendor/opensc' in default.stderr, default.stderr
    assert '-DSIGN_EMBED_OPENSC=OFF' in default.stderr, default.stderr
    disabled = subprocess.run([cmake, '-S', str(root), '-B', str(root / 'disabled'),
                               '-DSIGN_EMBED_OPENSC=OFF'],
                              capture_output=True, text=True, timeout=30)
    assert disabled.returncode == 0, disabled.stderr
    assert 'using external PKCS#11 modules' in disabled.stdout, disabled.stdout
