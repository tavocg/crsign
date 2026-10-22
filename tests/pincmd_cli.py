# SPDX-License-Identifier: GPL-3.0-or-later
"""Global/profile PIN helpers, precedence, bounded output and failure privacy."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sign, args_driver = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='sign-pincmd-') as directory:
    root = Path(directory)
    env = os.environ.copy()
    env['XDG_CONFIG_HOME'] = str(root / 'config')
    env['SIGN_MOCK_LOGIN'] = '1'
    env.pop('SIGN_MOCK_PROTECTED', None)
    env.pop('SIGN_MOCK_SIGN_FAIL', None)
    source, target = root / 'input.txt', root / 'signed.txt'
    source.write_bytes(b'PIN helper fixture')
    env['SIGN_MOCK_DATA'] = str(source)
    config = Path(env['XDG_CONFIG_HOME']) / 'crsign' / 'crsign.toml'
    config.parent.mkdir(parents=True)
    marker = root / 'calls'
    helper = root / 'helper ñ 文件.py'
    helper.write_text('''import os, sys
from pathlib import Path
mode = sys.argv[1]
with open(sys.argv[2], 'a') as file:
    file.write(mode + '\\n')
if mode == 'stdin':
    assert not sys.stdin.buffer.read()
if mode == 'literal':
    assert sys.argv[3:] == ['$HOME', '|', '>', '']
if mode == 'fail':
    sys.stderr.write('FAKE-SECRET-ERROR')
    sys.stdout.write('FAKE-SECRET-OUTPUT')
    sys.exit(7)
if mode == 'flood':
    while True:
        os.write(1, b'FAKE-SECRET-OVERFLOW' * 4096)
outputs = {'raw': b'1234', 'lf': b'1234\\n', 'crlf': b'1234\\r\\n',
           'stdin': b'1234', 'literal': b'1234', 'empty': b'', 'blank': b'\\n',
           'multi': b'1234\\nFAKE-SECRET-SECOND', 'nul': b'1234\\x00hidden',
           'invalid_utf8': b'\\xff', 'spaces': b'1234 ', 'cr': b'1234\\r',
           'limit': b'7' * 1024 + b'\\r\\n', 'large': b'7' * 1025 + b'\\n',
           'unicode': '12ñ\\n'.encode('utf-8')}
sys.stdout.buffer.write(outputs[mode])
''', encoding='utf-8')

    def command(mode):
        return [sys.executable, str(helper), mode, str(marker)]

    def toml(value):
        return json.dumps(value, ensure_ascii=False)

    def write(global_command=None, profile_command=None, *, profile=True):
        text = 'version = 0.1\n'
        if global_command is not None:
            text += 'pincmd = ' + toml(global_command) + '\n'
        if profile:
            text += '[[profiles]]\nlabel = "bccr"\n'
            if profile_command is not None:
                text += 'pincmd = ' + toml(profile_command) + '\n'
        config.write_text(text, encoding='utf-8')

    def sign_file(*args, success=True):
        if target.exists(): target.unlink()
        marker.unlink(missing_ok=True)
        result = subprocess.run([sign, str(source), '-l', 'B-B', '-o', str(target),
                                 '--json', *args], cwd=root, env=env, input=b'not-a-pin',
                                capture_output=True, timeout=10)
        assert (result.returncode == 0) == success, (result.stdout, result.stderr)
        assert b'PIN: ' not in result.stderr
        for secret in [b'FAKE-SECRET', b'1234', b'77777777']:
            assert secret not in result.stdout and secret not in result.stderr, result
        if success:
            assert json.loads(result.stdout)['ok']
            assert target.read_bytes() == source.read_bytes()
        else:
            assert not target.exists()
        return result

    def options(*args, success=True):
        result = subprocess.run([args_driver, str(source), *args], cwd=root, env=env,
                                capture_output=True, text=True, encoding='utf-8', timeout=10)
        assert (result.returncode == 0) == success, result.stderr
        return json.loads(result.stdout) if success else result

    # Globals work without profiles, including helpers with Unicode paths.
    for mode in ['raw', 'lf', 'crlf', 'stdin']:
        write(command(mode), profile=False)
        sign_file()
        assert marker.read_text().splitlines() == [mode]
    write(shlex.join(command('raw')), profile=False)
    assert options()['pincmd'] == command('raw')
    sign_file()
    for mode, expected in [('limit', '7' * 1024), ('unicode', '12ñ')]:
        env['SIGN_MOCK_EXPECTED_PIN'] = expected
        write(command(mode))
        sign_file('-p', 'bccr')
    del env['SIGN_MOCK_EXPECTED_PIN']

    # Inherit, override and disable globals; read configuration without running it.
    write(command('fail'))
    assert options('-p', 'bccr')['pincmd'] == command('fail')
    marker.unlink(missing_ok=True)
    options()
    assert not marker.exists()
    write(command('fail'), command('raw'))
    assert options('-p', 'bccr')['pincmd'] == command('raw')
    sign_file('-p', 'bccr')
    assert marker.read_text().splitlines() == ['raw']
    write(profile_command=shlex.join(command('raw')))
    sign_file('-p', 'bccr')
    write(command('fail'), [])
    assert options('-p', 'bccr')['pincmd'] == []
    result = sign_file('-p', 'bccr', success=False)
    assert b'interactive terminal is required' in result.stderr and not marker.exists()
    if os.name == 'posix':
        sys.path.insert(0, str(Path(__file__).parent / 'terminal'))
        sys.dont_write_bytecode = True
        from terminal_test import interactive
        output, _ = interactive([sign, str(source), '-p', 'bccr', '-lB-B',
                                 '-o', str(target), '--json'], env=env, stars=4)
        assert json.loads(output)['ok'] and target.exists()
        assert not marker.exists()

    # Quoting is supported; shell operators and variables remain literal arguments.
    literal = command('literal') + ['$HOME', '|', '>', '']
    for value in [literal, shlex.join(literal)]:
        write(value)
        sign_file()

    # No retries or interactive fallback when a configured helper fails.
    for mode in ['fail', 'empty', 'blank', 'multi', 'nul', 'invalid_utf8', 'large', 'flood', 'cr']:
        write(command(mode))
        result = sign_file(success=False)
        assert b'pincmd failed' in result.stderr
        assert marker.read_text().splitlines() == [mode]
    write(command('spaces'))
    result = sign_file(success=False)
    assert b'pincmd failed' not in result.stderr  # Spaces are not stripped.
    write(['missing-pincmd-FAKE-SECRET'])
    assert b'cannot start pincmd' in sign_file(success=False).stderr

    # Invalid settings are rejected even without -p, and never execute commands.
    for invalid in [True, 12, {}, [1], [''], ' ', '', '"unterminated', 'foo\x00bar']:
        write(invalid)
        result = options(success=False)
        assert 'pincmd' in result.stderr and str(config) in result.stderr
        write(profile_command=invalid)
        assert 'pincmd' in options('-p', 'bccr', success=False).stderr

    # PIN helpers are ignored when the token requires no PIN or has its own keypad.
    write(command('fail'))
    env['SIGN_MOCK_PROTECTED'] = '1'
    sign_file()
    assert not marker.exists()
    del env['SIGN_MOCK_PROTECTED']
    del env['SIGN_MOCK_LOGIN']
    sign_file()
    assert not marker.exists()

    # Other commands and help do not read the signing configuration.
    config.write_text('invalid TOML')
    for arguments in [('tokens', '--json'), ('--help',)]:
        result = subprocess.run([sign, *arguments], env=env, capture_output=True, timeout=10)
        assert result.returncode == 0, result.stderr
print('Global/profile PIN helper checks passed')
