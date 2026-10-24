#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage a relocatable native CLI/GTK archive from an already tested build.

No dependency downloads or vendor PKCS#11 drivers. Missing runtime dependencies
and conflicting library filenames fail packaging instead of producing a partial archive.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tarfile


def command(*args, env=None):
    return subprocess.check_output(list(map(str, args)), text=True, encoding='utf-8', errors='replace', env=env)


def pe_dependencies(text):
    return [name.strip() for name in re.findall(r'DLL Name:\s*([^\r\n]+)', text)]


def macho_commands(text):
    dependencies, rpaths, identity = [], [], False
    current = ''
    for line in map(str.strip, text.splitlines()):
        if line.startswith('cmd '):
            current = line[4:]
            identity |= current == 'LC_ID_DYLIB'
        elif current == 'LC_RPATH' and line.startswith('path '):
            rpaths.append(line[5:].rsplit(' (offset ', 1)[0])
        elif current in {'LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB',
                         'LC_LAZY_LOAD_DYLIB', 'LC_LOAD_UPWARD_DYLIB'} and line.startswith('name '):
            dependencies.append(line[5:].rsplit(' (offset ', 1)[0])
    return dependencies, rpaths, identity


class Bundle:
    def __init__(self, destination, search, platform=None):
        self.root = destination
        self.platform = platform or sys.platform
        self.windows = self.platform == 'win32'
        self.search = list(search)
        self.copied = {}
        self.pending = []
        self.images = []
        system = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32'
        self.system_dlls = {p.name.lower() for p in system.glob('*')} if self.windows else set()

    def copy(self, source, relative):
        source = source.resolve(strict=True)
        target = self.root / relative
        key = str(relative).lower() if self.windows else str(relative)
        if key in self.copied:
            if source == target.resolve():
                return target
            if self.copied[key] != source and not self.copied[key].samefile(source):
                raise RuntimeError(f'Runtime filename collision: {source} and {self.copied[key]}')
            return target
        self.copied[key] = source
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.resolve() != source:
            shutil.copy2(source, target)
        self.pending.append((source, target))
        return target

    def find(self, dependency, source, rpaths):
        if self.windows:
            name = dependency.lower()
            if name.startswith(('api-ms-win-', 'ext-ms-win-')) or name in self.system_dlls:
                return None
            for directory in [source.parent, *self.search]:
                candidate = directory / dependency
                if candidate.is_file():
                    return candidate
                if directory.is_dir():
                    matches = [p for p in directory.iterdir() if p.name.lower() == name]
                    if matches:
                        return matches[0]
        else:
            if dependency.startswith(('/System/Library/', '/usr/lib/', '/Library/Apple/System/Library/')):
                return None
            def expand(path):
                return path.replace('@loader_path', str(source.parent)).replace('@executable_path', str(self.executable_source.parent))
            candidates = [Path(expand(dependency))]
            if dependency.startswith('@rpath/'):
                tail = dependency[len('@rpath/'):]
                candidates = [Path(expand(path)) / tail for path in rpaths]
            candidates.extend(directory / Path(dependency).name for directory in self.search)
            for path in candidates:
                if path.is_file():
                    return path
        raise RuntimeError(f'Unresolved runtime dependency {dependency!r} of {source}')

    def close(self):
        if not self.pending:
            raise RuntimeError('No executable to package')
        self.executable_source = self.pending[0][0]
        while self.pending:
            source, target = self.pending.pop(0)
            if self.windows:
                dependencies = pe_dependencies(command('objdump', '-p', source))
                rpaths, identity = [], False
            else:
                dependencies, rpaths, identity = macho_commands(command('otool', '-l', source))
            changes = []
            for dependency in dependencies:
                path = self.find(dependency, source, rpaths)
                if path is None:
                    continue
                name = Path(dependency).name
                copied = self.copy(path, Path('bin' if self.windows else 'lib') / name)
                if not self.windows:
                    replacement = '@loader_path/' + os.path.relpath(copied, target.parent).replace(os.sep, '/')
                    changes.append((dependency, replacement))
            if not self.windows:
                target.chmod(target.stat().st_mode | stat.S_IWUSR)
                args = ['install_name_tool']
                for old, new in changes:
                    args.extend(['-change', old, new])
                for rpath in dict.fromkeys(rpaths):
                    args.extend(['-delete_rpath', rpath])
                if identity:
                    args.extend(['-id', '@rpath/' + target.name])
                if len(args) > 1:
                    subprocess.run([*args, str(target)], check=True)
                self.images.append(target)
        if not self.windows:
            # Rewriting Mach-O invalidates existing signatures. No developer ID
            # credentials are needed for local ad-hoc signatures.
            for image in self.images:
                subprocess.run(['codesign', '--force', '--sign', '-', str(image)], check=True)


def copy_data(source, target):
    if source.is_dir():
        shutil.copytree(source, target, dirs_exist_ok=True)


def stage_gui_resources(bundle, prefixes):
    root = bundle.root
    for prefix in prefixes:
        for relative in ('share/glib-2.0/schemas', 'share/icons/Adwaita', 'share/icons/hicolor'):
            copy_data(prefix / relative, root / relative)
        for parent, destination in [('lib/gio/modules', 'lib/gio/modules'),
                                    ('lib/gdk-pixbuf-2.0/2.10.0/loaders', 'lib/gdk-pixbuf-loaders')]:
            directory = prefix / parent
            for pattern in ('*.dll', '*.so', '*.dylib'):
                for module in directory.glob(pattern):
                    bundle.copy(module, Path(destination) / module.name)


def configure_gui_resources(bundle):
    root = bundle.root
    environment = os.environ.copy()
    if not bundle.windows:
        # Query tools and relocated modules must use the same GLib/GObject
        # libraries, rather than loading another copy from Homebrew.
        environment['DYLD_LIBRARY_PATH'] = str(root / 'lib')
    schemas = root / 'share/glib-2.0/schemas'
    if not list(schemas.glob('*.gschema.xml')):
        raise RuntimeError('Missing GTK GSettings schemas')
    subprocess.run(['glib-compile-schemas', str(schemas)], check=True)
    gio = root / 'lib/gio/modules'
    if gio.is_dir():
        subprocess.run(['gio-querymodules', str(gio)], check=True, env=environment)
    loaders = sorted((root / 'lib/gdk-pixbuf-loaders').glob('*'))
    if loaders:
        tool = shutil.which('gdk-pixbuf-query-loaders')
        if not tool:
            raise RuntimeError('Missing gdk-pixbuf-query-loaders')
        output = command(tool, *loaders, env=environment)
        names = {module.name for module in loaders}
        found, lines = set(), []
        for line in output.splitlines():
            if re.fullmatch(r'"[^"\n]+\.(dll|so|dylib)"', line):
                name = line[1:-1].replace('\\', '/').rsplit('/', 1)[-1]
                if name not in names:
                    raise RuntimeError(f'Unexpected image loader: {name}')
                line = f'"@CRSIGN_PREFIX@/lib/gdk-pixbuf-loaders/{name}"'
                found.add(name)
            lines.append(line)
        if found != names:
            raise RuntimeError(f'Image loaders failed to initialize: {names - found}')
        templates = root / 'share/crsign'
        templates.mkdir(parents=True, exist_ok=True)
        (templates / 'loaders.cache.in').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    fonts = root / 'etc/fonts'
    fonts.mkdir(parents=True)
    directories = ['WINDOWSFONTDIR'] if bundle.windows else ['/System/Library/Fonts', '/Library/Fonts', '~/Library/Fonts']
    config = '<?xml version="1.0"?>\n<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">\n<fontconfig>\n'
    config += ''.join(f'<dir>{path}</dir>\n' for path in directories)
    config += '<cachedir prefix="xdg">fontconfig</cachedir>\n</fontconfig>\n'
    (fonts / 'fonts.conf').write_text(config, encoding='utf-8')


def sources_archive(source, target):
    with tarfile.open(target, 'w:gz') as archive:
        for checkout, prefix in [(source, Path('crsign')), (source / 'tests/libssig', Path('crsign/tests/libssig'))]:
            files = subprocess.check_output(['git', '-C', str(checkout), 'ls-files', '-z']).decode().split('\0')
            for name in filter(None, files):
                path = checkout / name
                if path.is_file():
                    archive.add(path, arcname=str(prefix / name), recursive=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--libssig-build', type=Path, help='External libssig build; defaults to --build')
    parser.add_argument('--library-prefix', type=Path, required=True)
    parser.add_argument('--dependency-prefix', type=Path, action='append', default=[])
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform not in ('win32', 'darwin'):
        parser.error('Native packaging is supported on Windows and macOS')
    if not shutil.which('git'):
        parser.error('Git is required to include the tracked project and libssig sources')
    cache = (args.build / 'CMakeCache.txt').read_text(encoding='utf-8')
    library_cache = ((args.libssig_build or args.build) / 'CMakeCache.txt').read_text(encoding='utf-8')
    if 'LIBSSIG_EMBED_MAGIC_DATABASE:BOOL=ON' not in library_cache:
        parser.error('Build libssig with LIBSSIG_EMBED_MAGIC_DATABASE=ON first')
    root = args.destination.resolve()
    if root.exists():
        parser.error('Destination must not exist; use a fresh staging directory')
    root.mkdir(parents=True)
    prefixes = [args.library_prefix.resolve(), *[p.resolve() for p in args.dependency_prefix]]
    search = [p / sub for p in prefixes for sub in ('bin', 'lib')]
    subprocess.run(['cmake', '--install', str(args.build), '--config', 'Release', '--prefix', str(root)], check=True)
    executable = Path('bin/sign.exe' if sys.platform == 'win32' else 'bin/sign')
    bundle = Bundle(root, search)
    bundle.copy(root / executable, executable)
    gui = 'HEADLESS:BOOL=OFF' in cache
    if gui:
        stage_gui_resources(bundle, prefixes[1:])
    bundle.close()
    if gui:
        configure_gui_resources(bundle)
    project = Path(__file__).resolve().parents[1]
    data = root / 'share/crsign'
    data.mkdir(parents=True, exist_ok=True)
    (data / 'portable').write_text('1\n')
    shutil.copy2(project / 'LICENSE', data / 'LICENSE')
    shutil.copy2(project / 'README.md', data / 'README.md')
    sources_archive(project, data / 'source.tar.gz')
    for prefix in prefixes:
        copy_data(prefix / 'share/crsign-deps', root / 'share/crsign-deps')
        copy_data(prefix / 'share/licenses', root / 'share/licenses')
    manifest = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(root.rglob('*')) if path.is_file()}
    (data / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    output = shutil.make_archive(str(root), 'zip' if bundle.windows else 'gztar', root.parent, root.name)
    print(f'Created {output}')


if __name__ == '__main__':
    main()
