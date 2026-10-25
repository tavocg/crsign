#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build tomlc17 and optional argp from pinned sources.

Network access is explicit here; application/library CMake configure stays offline.
Cached archives are always verified, including when --offline is used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

SOURCES = {
    'tomlc17': ('R20260501', 'https://codeload.github.com/cktan/tomlc17/tar.gz/refs/tags/R20260501',
                '85a61daa8ca69c8036ce37529645769bf58ee4c8b0734283228c854cecf55cda'),
    'argp': ('1.2.0', 'https://codeload.github.com/tom42/argp-standalone/tar.gz/refs/tags/v1.2.0',
             '0c298d55c5df1fd5510308b7b5b2617d2346489127a798d0c9999b7843d3eb33'),
}



def fetch(name, cache, offline):
    version, url, digest = SOURCES[name]
    archive = cache / f'{name}-{version}.tar.gz'
    if not archive.exists():
        if offline:
            raise RuntimeError(f'Missing cached archive: {archive}')
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f'Checksum mismatch: {name}')
        archive.write_bytes(data)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f'Checksum mismatch: {archive}')
    directory = cache / f'{name}-source'
    directory.mkdir(exist_ok=True)
    with tarfile.open(archive) as source:
        # Accept regular source files/directories only; never follow archive links.
        for member in source:
            parts = Path(member.name).parts
            if not parts or Path(member.name).is_absolute() or '..' in parts:
                raise RuntimeError(f'Invalid archive path: {member.name}')
            target = directory.joinpath(*parts[1:])
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as inp, target.open('wb') as out:
                    shutil.copyfileobj(inp, out)
            elif member.issym():
                # Never follow or create archive symlinks.
                continue
            else:
                raise RuntimeError(f'Unsupported archive member: {member.name}')
    return directory, archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--cache', type=Path, default=Path('build/native-deps'))
    parser.add_argument('--with-argp', action='store_true', help='Build Windows-compatible argp (also testable on Linux)')
    parser.add_argument('--offline', action='store_true')
    args = parser.parse_args()
    cache, prefix = args.cache.resolve(), args.prefix.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    names = ['tomlc17', 'argp'] if args.with_argp else ['tomlc17']
    inputs = {name: fetch(name, cache, args.offline) for name in names}
    project = cache / 'project'
    project.mkdir(exist_ok=True)
    toml = inputs['tomlc17'][0].as_posix()
    cmake = f'''cmake_minimum_required(VERSION 3.17.3)
project(crsign_native_dependencies LANGUAGES C)
add_library(tomlc17 STATIC "{toml}/src/tomlc17.c")
set_target_properties(tomlc17 PROPERTIES C_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
install(TARGETS tomlc17 ARCHIVE DESTINATION lib)
install(FILES "{toml}/src/tomlc17.h" DESTINATION include)
'''
    if args.with_argp:
        argp = inputs['argp'][0].as_posix()
        cmake += f'''set(BUILD_SHARED_LIBS OFF)
add_subdirectory("{argp}" argp)
set_target_properties(argp-standalone PROPERTIES OUTPUT_NAME argp POSITION_INDEPENDENT_CODE ON)
install(TARGETS argp-standalone ARCHIVE DESTINATION lib)
install(FILES "{argp}/include/argp-standalone/argp.h" DESTINATION include)
'''
    (project / 'CMakeLists.txt').write_text(cmake, encoding='utf-8')
    subprocess.run(['cmake', '-S', str(project), '-B', str(cache / 'build'),
                    '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_INSTALL_PREFIX={prefix}'], check=True)
    subprocess.run(['cmake', '--build', str(cache / 'build'), '--config', 'Release', '--parallel', '4'], check=True)
    subprocess.run(['cmake', '--install', str(cache / 'build'), '--config', 'Release'], check=True)
    notices = prefix / 'share/crsign-deps'
    notices.mkdir(parents=True, exist_ok=True)
    for name, (source, archive) in inputs.items():
        shutil.copy2(archive, notices / archive.name)
        for pattern in ('LICENSE*', 'COPYING*'):
            for license_file in source.glob(pattern):
                if license_file.is_file():
                    shutil.copy2(license_file, notices / f'{name}-{license_file.name}')
    (notices / 'sources.json').write_text(json.dumps({name: SOURCES[name] for name in names}, indent=2) + '\n')


if __name__ == '__main__':
    main()
