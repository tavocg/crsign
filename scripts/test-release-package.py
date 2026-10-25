#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise installed/extracted release binaries without developer dependencies."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def exercise(executable, directory, env, prefix=(), convert=str, openssl='openssl'):
    def run(*args, success=True):
        result = subprocess.run([*prefix, str(executable), *map(str, args)], env=env, cwd=directory,
                                capture_output=True, text=True, timeout=45)
        if (result.returncode == 0) != success:
            raise RuntimeError(f'{args}: exit {result.returncode}\n{result.stdout}\n{result.stderr}')
        return result
    assert 'Sign' in run('--version').stdout
    assert '--module' in run('--help').stdout
    assert json.loads(run('tokens', '--json').stdout)['ok']
    certificate, key, signed = [directory / p for p in ('root.pem', 'key.pem', 'document.p7m')]
    document = directory / 'documento ñ.txt'
    document.write_text('Release verification\n', encoding='utf-8')
    for arguments in [
        ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=Release test',
         '-addext', 'basicConstraints=critical,CA:TRUE', '-keyout', key, '-out', certificate],
        ['cms', '-sign', '-binary', '-nodetach', '-cades', '-in', document, '-signer', certificate,
         '-inkey', key, '-outform', 'DER', '-out', signed]]:
        subprocess.run([openssl, *map(str, arguments)], check=True, capture_output=True)
    assert json.loads(run('verify', convert(signed), '--ca-file', convert(certificate), '--json').stdout)['ok']
    bad = directory / 'corrupted.p7m'
    bad.write_bytes(b'not a signed document')
    assert not json.loads(run('verify', convert(bad), '--json', success=False).stdout)['ok']
    missing = run('tokens', '-m', convert(directory / 'missing-driver'), '--json', success=False)
    assert 'configured PKCS#11 modules' in missing.stderr
    log = directory / 'gui.log'
    with log.open('w') as output:
        gui = subprocess.Popen([*prefix, str(executable), '--gui'], env=env, cwd=directory,
                               stdout=output, stderr=subprocess.STDOUT)
        try:
            time.sleep(6)
            if gui.poll() is not None:
                raise RuntimeError(f'GUI exited with {gui.returncode}: {log.read_text()}')
        finally:
            gui.terminate()
            try:
                gui.wait(timeout=15)
            except subprocess.TimeoutExpired:
                gui.kill()
                gui.wait()
    assert not any(s in log.read_text() for s in ('Gtk-ERROR', 'GLib-GIO-ERROR', 'cannot initialize packaged'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--wine', action='store_true')
    parser.add_argument('--openssl', default='openssl')
    args = parser.parse_args()
    package = args.package.resolve()
    with tempfile.TemporaryDirectory(prefix='crsign-release-') as temporary:
        directory = Path(temporary)
        env = dict(os.environ, GSK_RENDERER='cairo', XDG_CONFIG_HOME=str(directory / 'config'),
                   XDG_CACHE_HOME=str(directory / 'cache'))
        for name in ('LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'GSETTINGS_SCHEMA_DIR', 'GIO_EXTRA_MODULES',
                     'GIO_MODULE_DIR', 'GDK_PIXBUF_MODULE_FILE', 'FONTCONFIG_FILE', 'FONTCONFIG_PATH',
                     'XDG_DATA_DIRS', 'MAGIC', 'OPENSSL_MODULES'):
            env.pop(name, None)
        if package.suffix.lower() == '.appimage':
            assert package.name == f'CRSign-linux-{os.uname().machine}.AppImage'
            # The clean test container needs neither FUSE nor GTK. Extraction
            # runs the real AppImage runtime and tests its relocatable AppDir.
            image = directory / package.name
            shutil.copy2(package, image)
            image.chmod(0o755)
            try:
                subprocess.run([str(image), '--appimage-extract'], cwd=directory, env=env,
                               check=True, stdout=subprocess.DEVNULL)
            except FileNotFoundError:
                # See package-appimage.py: NixOS binfmt handlers live outside
                # the test container. Only alter this disposable test copy.
                data = bytearray(image.read_bytes())
                if data[:4] != b'\x7fELF' or data[8:11] != b'AI\x02':
                    raise
                data[8:11] = b'\0\0\0'
                image.write_bytes(data)
                subprocess.run([str(image), '--appimage-extract'], cwd=directory, env=env,
                               check=True, stdout=subprocess.DEVNULL)
            appdir = directory / 'aplicación ñ'
            (directory / 'squashfs-root').rename(appdir)
            env['APPDIR'] = str(appdir)
            exercise(appdir / 'AppRun', directory, env, openssl=args.openssl)
        else:
            assert package.name == 'CRSign-windows-x86_64.msi'
            prefix = ['wine'] if args.wine else []
            if args.wine:
                env.update(WINEPREFIX=str(directory / 'wine'), WINEARCH='win64', WINEDEBUG='-all')
                subprocess.run(['wineboot', '-u'], env=env, check=True, stdout=subprocess.DEVNULL)
                def convert(path):
                    return subprocess.check_output(['winepath', '-w', str(path)], env=env, text=True).strip()
            else:
                convert = str
                # Installed application must resolve its own DLLs.
                system = Path(env['SystemRoot'])
                env['PATH'] = os.pathsep.join(map(str, [system / 'System32', system]))
            install = directory / 'instalación ñ'
            install_path = convert(install)
            def installer(action):
                log = directory / ('install.log' if action == '/i' else 'uninstall.log')
                result = subprocess.run([*prefix, 'msiexec', action, convert(package), '/qn', '/norestart',
                                         '/l*v', convert(log), 'INSTALLFOLDER=' + install_path], env=env, timeout=180)
                if result.returncode not in (0, 3010):
                    raise RuntimeError(f'MSI {action}: exit {result.returncode}\n{log.read_text(errors="replace")}')
            marker = directory / 'config/crsign/preserve.txt'
            marker.parent.mkdir(parents=True)
            marker.write_text('preserve user data')
            installer('/i')
            try:
                assert (install / 'bin/sign.exe').is_file()
                exercise(convert(install / 'bin/sign.exe') if args.wine else install / 'bin/sign.exe',
                         directory, env, prefix, convert, args.openssl)
            finally:
                installer('/x')
            assert not (install / 'bin/sign.exe').exists()
            assert not list(install.rglob('*.dll')) if install.exists() else True
            assert marker.read_text() == 'preserve user data'
        print('Release package passed: CLI, real signature verification, GUI and' +
              (' uninstall/data retention' if package.suffix.lower() == '.msi' else ' relocation'))


if __name__ == '__main__':
    main()
