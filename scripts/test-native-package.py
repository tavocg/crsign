#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the relocated archive without development DLL/dylib search paths."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('package', type=Path)
parser.add_argument('--openssl', required=True)
parser.add_argument('--gui', action='store_true')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='crsign-package-') as temporary:
    directory = Path(temporary)
    root = directory / 'instalación con espacios ñ'
    shutil.copytree(args.package, root)
    document = directory / 'documento ñ.txt'
    document.write_text('CRSign portable package verification\n', encoding='utf-8')
    certificate, key, signed = [directory / name for name in ('root.pem', 'key.pem', 'document.p7s')]
    def openssl(*arguments):
        subprocess.run([args.openssl, *map(str, arguments)], check=True, capture_output=True)
    openssl('req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=CRSign package test',
            '-addext', 'basicConstraints=critical,CA:TRUE', '-keyout', key, '-out', certificate)
    openssl('cms', '-sign', '-binary', '-nodetach', '-cades', '-in', document,
            '-signer', certificate, '-inkey', key, '-outform', 'DER', '-out', signed)
    env = dict(os.environ, XDG_CONFIG_HOME=str(directory / 'config'), XDG_CACHE_HOME=str(directory / 'cache'))
    for name in ('DYLD_LIBRARY_PATH', 'DYLD_FALLBACK_LIBRARY_PATH', 'LD_LIBRARY_PATH',
                 'GSETTINGS_SCHEMA_DIR', 'GIO_EXTRA_MODULES', 'GIO_MODULE_DIR',
                 'GDK_PIXBUF_MODULE_FILE', 'GDK_PIXBUF_MODULEDIR', 'FONTCONFIG_FILE',
                 'FONTCONFIG_PATH', 'XDG_DATA_DIRS', 'MAGIC'):
        env.pop(name, None)
    system = Path(env.get('SystemRoot', 'C:/Windows'))
    env['PATH'] = os.pathsep.join(map(str, [root / 'bin', system / 'System32', system])) if os.name == 'nt' else '/usr/bin:/bin'
    executable = root / 'bin' / ('sign.exe' if os.name == 'nt' else 'sign')
    def run(*arguments, success=True):
        result = subprocess.run([str(executable), *map(str, arguments)], env=env, cwd=directory,
                                capture_output=True, encoding='utf-8', timeout=30)
        assert (result.returncode == 0) == success, (arguments, result.returncode, result.stdout, result.stderr)
        return result
    assert 'Sign' in run('--version').stdout
    assert '--module' in run('--help').stdout
    result = json.loads(run('verify', signed, '--ca-file', certificate, '--json').stdout)
    assert result['ok'], result
    # Explicit missing drivers must fail clearly after relocation.
    result = run('tokens', '-m', directory / 'missing-driver', '--json', success=False)
    assert 'configured PKCS#11 modules' in result.stderr
    if args.gui:
        # Native CI runners have a desktop. Keep the window open long enough to
        # catch schema, image-loader, renderer and font initialization failures.
        env['GSK_RENDERER'] = 'cairo'
        process = subprocess.Popen([str(executable), '--gui'], env=env, cwd=directory,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            time.sleep(4)
            assert process.poll() is None, process.communicate()
        finally:
            if process.poll() is None:
                process.terminate()
            process.communicate(timeout=10)
print('Relocated package CLI, verification and module loading checks passed' +
      ('; GUI startup passed' if args.gui else ''))
