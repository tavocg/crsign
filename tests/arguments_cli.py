# SPDX-License-Identifier: GPL-3.0-or-later
"""Argument syntax must remain identical with libc and standalone argp."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sign = sys.argv[1]
with tempfile.TemporaryDirectory(prefix='sign-arguments-') as directory:
    root = Path(directory)
    env = os.environ.copy()
    env['XDG_CONFIG_HOME'] = str(root / 'config')
    config = Path(env['XDG_CONFIG_HOME']) / 'crsign' / 'crsign.toml'
    config.parent.mkdir(parents=True)
    config.write_text('version = 0.1\n[[profiles]]\nlabel = "perfil ñ 文件"\nhash = "sha384"\n', encoding='utf-8')
    source = 'documento ñ 文件.txt'

    def run(*args, success=True):
        result = subprocess.run([sign, *args], env=env, capture_output=True,
                                encoding='utf-8', timeout=10)
        assert (result.returncode == 0) == success, (args, result.stdout, result.stderr)
        return result

    def options(*args):
        return json.loads(run(*args).stdout)

    baseline = options(source, '-l', 'B-B', '-a', 'rsa-pss', '-H', 'sha512',
                       '-o', 'salida ñ 文件.p7m')
    assert baseline['input'] == source
    assert baseline['output'] == 'salida ñ 文件.p7m'
    assert options('-lB-B', '-arsa-pss', source, '-Hsha512',
                   '-o', 'salida ñ 文件.p7m') == baseline
    assert options('--level=B-B', '--algorithm=rsa-pss', '--hash=sha512',
                   '--output=salida ñ 文件.p7m', source) == baseline
    assert options(source, '-l', 'b-b', '-a', 'RSA-PSS', '-H', 'SHA512',
                   '-o', 'salida ñ 文件.p7m') == baseline
    assert options(source, '-o', 'old', '-o', 'salida ñ 文件.p7m', '-lB-B',
                   '-arsa-pss', '-Hsha512') == baseline
    assert options(source, '-A', '-p')['authority'] == '-p'
    assert options('--', '-p')['input'] == '-p'
    assert options('--', '--hash=sha512')['input'] == '--hash=sha512'
    assert options('--profile=perfil ñ 文件', source)['hash'] == options(source, '-Hsha384')['hash']
    assert options(source, '-pperfil ñ 文件', '-Hsha256') == options(source, '-Hsha256')
    assert options('-Hsha256', source, '-p', 'perfil ñ 文件') == options(source, '-Hsha256')
    modules = options(source, '-m', 'módulo ñ 文件.so', '--module=other.so')['modules']
    assert list(map(Path, modules)) == [Path('módulo ñ 文件.so').resolve(), Path('other.so').resolve()]
    assert options('-mmódulo ñ 文件.so', source, '--module', 'other.so')['modules'] == modules
    # CLI module lists are dynamic, without a fixed maximum inherited from bundles.
    paths = [f'module-{i}.so' for i in range(80)]
    arguments = [arg for path in paths for arg in ('-m', path)]
    assert list(map(Path, options(source, *arguments)['modules'])) == [Path(path).resolve() for path in paths]
    run('-vo', 'unused', source, success=False)
    for args in [(source, '--hash'), (source, '--unknown'), (source, '-p'),
                 (source, 'extra'), (source, '--hash=invalid'),
                 (source, '-m'), (source, '--module='), (source, '-m', '')]:
        run(*args, success=False)
    for flag in ['--help', '-h', '-?', '--usage', '--version', '-V', '-gh']:
        assert run(flag).stdout
print('Portable argument parsing checks passed')
