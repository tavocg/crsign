#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
python3 scripts/bootstrap-native.py --prefix "$PWD/build/native-prefix"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DHEADLESS=OFF -DSIGN_EMBED_OPENSC=ON -DSIGN_TEST_LIBSSIG=OFF \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$PWD/build/native-prefix" \
  -DSIGN_VERSION="${RELEASE_VERSION:-0.1.0}"
cmake --build build --parallel 4
ctest --test-dir build --parallel 4 --output-on-failure
python3 scripts/package-appimage.py --build build \
  --output "build/dist/CRSign-linux-$(uname -m).AppImage"
