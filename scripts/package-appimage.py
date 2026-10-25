#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bundle the tested Linux executable and resources with pinned AppImage tools."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import urllib.request

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native', PROJECT / 'scripts/package-native.py')
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


def fetch_tool(name, arch, cache):
    metadata = json.loads((PROJECT / 'packaging/release/appimage-tools.json').read_text())[name][arch]
    target = cache / (name + '-' + arch)
    if not target.exists():
        with urllib.request.urlopen(metadata['url'], timeout=60) as response:
            target.write_bytes(response.read())
    if hashlib.sha256(target.read_bytes()).hexdigest() != metadata['sha256']:
        raise RuntimeError(f'Checksum mismatch: {name}/{arch}')
    target.chmod(0o755)
    return target


def run_appimage(image, *args, env):
    try:
        subprocess.run([str(image), *map(str, args)], check=True, env=env)
    except FileNotFoundError:
        # A NixOS host may register an AppImage binfmt interpreter that is not
        # available inside Docker. Bypass that handler in a private tool copy;
        # the verified download and generated release asset remain unchanged.
        data = bytearray(image.read_bytes())
        if data[:4] != b'\x7fELF' or data[8:11] != b'AI\x02':
            raise
        copied = image.with_suffix('.elf')
        data[8:11] = b'\0\0\0'
        copied.write_bytes(data)
        copied.chmod(0o755)
        subprocess.run([str(copied), *map(str, args)], check=True, env=env)


class Stage:
    platform, windows = 'linux', False

    def __init__(self, root):
        self.root = root

    def copy(self, source, relative):
        target = self.root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def runtime_libraries(executables):
    # linuxdeploy's default blacklist assumes some desktop libraries exist on
    # the host. Explicitly request the closure needed by our GTK4 application.
    system = re.compile(r'^(lib(c|m|dl|pthread|rt|resolv|nss_.*)\.so|ld-linux|'
                        r'lib(GL|EGL|GLX|GLdispatch|vulkan|gbm|drm).*\.so)')
    libraries = set()
    for executable in executables:
        output = subprocess.check_output(['ldd', str(executable)], text=True)
        if 'not found' in output:
            raise RuntimeError(f'Unresolved build dependency of {executable}: {output}')
        for name, path in re.findall(r'^\s*(\S+)\s+=>\s+(/\S+)', output, re.MULTILINE):
            if not system.match(name):
                libraries.add(path)
    return sorted(libraries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    arch = platform.machine()
    if arch not in ('x86_64', 'aarch64'):
        parser.error('Supported native architectures: x86_64 and aarch64')
    build = args.build.resolve()
    appdir = build / 'AppDir'
    if appdir.exists():
        shutil.rmtree(appdir)
    prefix = appdir / 'usr'
    subprocess.run(['cmake', '--install', str(build), '--prefix', str(prefix)], check=True)
    native.stage_gui_resources(Stage(prefix), [Path('/usr')])
    # Ubuntu installs runtime modules in a multiarch libdir.
    multiarch = Path(subprocess.check_output(['pkg-config', '--variable=libdir', 'gdk-pixbuf-2.0'], text=True).strip())
    for parent, destination in [('gdk-pixbuf-2.0/2.10.0/loaders', 'lib/gdk-pixbuf-loaders'),
                                ('gio/modules', 'lib/gio/modules'), ('ossl-modules', 'lib/ossl-modules')]:
        for module in (multiarch / parent).glob('*.so'):
            Stage(prefix).copy(module, Path(destination) / module.name)
    data = prefix / 'share/crsign'
    data.mkdir(parents=True, exist_ok=True)
    (data / 'portable').write_text('1\n')
    for name in ('LICENSE', 'README.md'):
        shutil.copy2(PROJECT / name, data / name)
    native.sources_archive(PROJECT, data / 'source.tar.gz')
    native.copy_data(build / 'native-prefix/share/crsign-deps', prefix / 'share/crsign-deps')
    for copyright in Path('/usr/share/doc').glob('*/copyright'):
        target = prefix / 'share/licenses' / copyright.parent.name
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(copyright, target / 'copyright')
    shutil.copy2(PROJECT / 'packaging/release/crsign.desktop', appdir / 'org.crsign.Sign.desktop')
    subprocess.run(['rsvg-convert', '-w', '256', '-h', '256',
                    '-o', str(appdir / 'org.crsign.Sign.png'), str(PROJECT / 'packaging/icon-rounded.svg')], check=True)
    tools = build / 'appimage-tools'
    tools.mkdir(exist_ok=True)
    deploy = fetch_tool('linuxdeploy', arch, tools)
    image_tool = fetch_tool('appimagetool', arch, tools)
    runtime = fetch_tool('runtime', arch, tools)
    environment = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN='1', ARCH=arch)
    pcsc = Path(subprocess.check_output(['pkg-config', '--variable=libdir', 'libpcsclite'], text=True).strip()) / 'libpcsclite.so.1'
    libraries = runtime_libraries([prefix / 'bin/sign', *prefix.glob('lib/**/*.so')])
    arguments = [part for library in libraries for part in ('--library', library)]
    run_appimage(deploy, '--appdir', str(appdir), '--executable', str(prefix / 'bin/sign'),
                    '--desktop-file', str(appdir / 'org.crsign.Sign.desktop'), '--icon-file', str(appdir / 'org.crsign.Sign.png'),
                    '--library', str(pcsc), *arguments, env=environment)
    native.configure_gui_resources(Stage(prefix))
    # linuxdeploy's default AppRun is a symlink to sign; never overwrite sign.
    (appdir / 'AppRun').unlink(missing_ok=True)
    shutil.copy2(PROJECT / 'packaging/release/AppRun', appdir / 'AppRun')
    (appdir / 'AppRun').chmod(0o755)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    run_appimage(image_tool, '--runtime-file', str(runtime), str(appdir), str(args.output.resolve()), env=environment)


if __name__ == '__main__':
    main()
