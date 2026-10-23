#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise configuration and actual managed PKCS#11 dynamic loading."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sign, driver, mock, first, second, broken = map(lambda x: str(Path(x).resolve()), sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='sign-modules-') as temporary:
    root = Path(temporary)
    env = dict(os.environ, HOME=str(root / 'home'), XDG_CONFIG_HOME=str(root / 'config'))
    directory = Path(env['XDG_CONFIG_HOME']) / 'crsign'
    directory.mkdir(parents=True)
    config = directory / 'crsign.toml'
    log = root / 'modules.log'
    env['SIGN_TEST_MODULE_LOG'] = str(log)
    source = root / 'input.txt'
    source.write_text('module selection')
    env['SIGN_MOCK_DATA'] = str(source)
    # p11-kit discovers only modules explicitly registered in this isolated HOME
    # plus system registrations; distinguish our fixtures by their unique labels.
    registrations = Path(env['XDG_CONFIG_HOME']) / 'pkcs11' / 'modules'
    registrations.mkdir(parents=True)
    (registrations / 'sign-test.module').write_text(f'module: {first}\n')

    def write(global_modules=None, profile_modules=None, extra=''):
        text = 'version = 0.1\n'
        if global_modules is not None:
            text += 'modules = ' + json.dumps(global_modules) + '\n'
        text += '[[profiles]]\nlabel = "bccr"\n'
        if profile_modules is not None:
            text += 'modules = ' + json.dumps(profile_modules) + '\n'
        config.write_text(text + extra)

    def run(program, *args, success=True):
        result = subprocess.run([program, *map(str, args)], env=env,
                                capture_output=True, text=True, timeout=10)
        assert (result.returncode == 0) == success, (args, result.stdout, result.stderr)
        return result

    def labels(*args):
        if log.exists():
            log.unlink()
        data = json.loads(run(sign, 'tokens', '--json', *args).stdout)
        return {token['label'] for token in data['tokens'] if token['label'].startswith('Sign-')}

    def options(*args):
        return [str(Path(path)) for path in json.loads(run(driver, source, *args).stdout)['modules']]

    # No configuration, omitted modules and [] preserve registered discovery.
    assert labels() == {'Sign-first'}
    assert labels('-m', second) == {'Sign-second'}
    assert options('-m', second) == [second]
    write()
    assert labels() == {'Sign-first'}
    write([])
    assert labels() == {'Sign-first'}
    write([second])
    assert labels() == {'Sign-second'}
    assert labels('-p', 'bccr') == {'Sign-second'}
    assert options('-p', 'bccr') == [second]
    write([second], [])
    assert labels('-p', 'bccr') == {'Sign-first'}
    assert options('-p', 'bccr') == []
    write([broken], [second, first, second])
    assert options('-p', 'bccr') == [second, first]
    assert labels('-p', 'bccr') == {'Sign-first', 'Sign-second'}
    events = log.read_text().splitlines()
    for name in ['first', 'second']:
        assert events.count(f'Sign-{name}:initialize') == 1, events
        assert events.count(f'Sign-{name}:finalize') == 1, events
    assert not any('broken' in event for event in events)

    # CLI lists replace both global and profile lists, regardless of argument order.
    write([broken], [broken])
    assert labels('-m', second) == {'Sign-second'}
    assert labels('-m', second, '-p', 'bccr') == {'Sign-second'}
    assert labels('-p', 'bccr', '--module=' + first, '-m' + second) == {'Sign-first', 'Sign-second'}
    assert options('-m', second, '-p', 'bccr', '--module', first, '-m', second) == [second, first]
    events = log.read_text().splitlines()
    assert not any('broken' in event for event in events), events
    # Multiple CLI modules go through real managed loading and retain deduplication.
    assert labels('-m', second, '-m', first, '-m', second) == {'Sign-first', 'Sign-second'}
    events = log.read_text().splitlines()
    for name in ['first', 'second']:
        assert events.count(f'Sign-{name}:initialize') == 1, events
        assert events.count(f'Sign-{name}:finalize') == 1, events

    # Relative/Unicode paths, lexical deduplication and profile signing options.
    local = directory / ('módulo con espacios' + Path(second).suffix)
    shutil.copyfile(second, local)
    write([first], [local.name, './' + local.name], 'token = "irrelevant"\nhash = "sha512"\n')
    assert options('-p', 'bccr') == [str(local)]
    assert labels('-p', 'bccr') == {'Sign-second'}
    relative = os.path.relpath(local, Path.cwd())
    assert options('-m', relative) == [str(local)]
    assert labels('-m', relative) == {'Sign-second'}
    # CLI values have no delimiter syntax; commas and semicolons belong to paths.
    punctuation = directory / ('module,one;two' + Path(second).suffix)
    shutil.copyfile(second, punctuation)
    assert labels('--module', str(punctuation)) == {'Sign-second'}
    assert options('--module', str(punctuation)) == [str(punctuation)]
    # The real signing path also uses configured modules (fixture has no certs).
    selected = json.loads(run(sign, 'tokens', '--json', '-p', 'bccr').stdout)['tokens'][0]['uri']
    result = run(sign, source, '-p', 'bccr', '-t', selected, success=False)
    assert 'no matching signing certificate' in result.stderr, result.stderr
    # The signing path uses CLI modules as well, overriding a broken profile.
    write([broken], [broken])
    result = run(sign, source, '-p', 'bccr', '-m', str(local), success=False)
    assert 'no matching signing certificate' in result.stderr, result.stderr
    # Exercise profile/module ownership across a successful signing operation.
    write([first], [second], 'level = "B-B"\n')
    run(mock, source, '-p', 'bccr', '-o', root / 'signed.p7s')
    run(mock, source, '-m', str(local), '-p', 'bccr', '-o', root / 'signed-cli.p7s')

    # Every explicit module must load and initialize; no fallback to discovery.
    for failing in [str(root / 'missing.so'), broken]:
        write([second, failing])
        log.unlink(missing_ok=True)
        result = run(sign, 'tokens', '--json', success=False)
        assert 'configured PKCS#11 modules' in result.stderr
        events = log.read_text().splitlines()
        assert 'Sign-second:initialize' in events and 'Sign-second:finalize' in events, events
        assert not any('Sign-first' in event for event in events)
        write([failing])
        result = run(sign, source, success=False)
        assert 'configured PKCS#11 modules' in result.stderr

    write([first])
    log.unlink(missing_ok=True)
    result = run(sign, 'tokens', '-m', second, '-m', broken, success=False)
    assert 'configured PKCS#11 modules' in result.stderr
    events = log.read_text().splitlines()
    assert 'Sign-second:finalize' in events and not any('Sign-first' in event for event in events)
    # Empty/missing paths and incompatible commands are rejected before loading.
    for args in [('tokens', '-m', ''), ('tokens', '-m'),
                 ('verify', source, '-m', first), ('-m', first, 'verify', source),
                 ('--gui', '-m', first), ('-m', first)]:
        run(sign, *args, success=False)

    # Reject invalid schema globally and inside a selected profile, including NULs.
    for invalid in ['path', True, 12, {}, [''], [12], ['bad\x00path']]:
        for global_value, profile_value, args in [(invalid, None, ()), (None, invalid, ('-p', 'bccr'))]:
            write(global_value, profile_value)
            # JSON booleans/objects are not always TOML, but all must be rejected.
            result = run(driver, source, *args, success=False)
            assert str(config) in result.stderr, result.stderr
    write([first])
    run(sign, 'tokens', '-p', 'missing', success=False)
    run(sign, 'tokens', '-p', 'bccr', '-t', 'invalid', success=False)
    run(sign, 'verify', source, '-p', 'bccr', success=False)
print('Global/profile modules, discovery and real dynamic loading checks passed')
