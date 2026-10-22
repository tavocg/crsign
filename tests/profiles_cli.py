# SPDX-License-Identifier: GPL-3.0-or-later
"""Profile schema, option precedence, and signing with automatic selection."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

args_driver, sign = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='sign-profiles-') as directory:
    root = Path(directory)
    env = os.environ.copy()
    env.pop('NIX_SSL_CERT_FILE', None)
    env['HOME'] = str(root / 'home')
    env['XDG_CONFIG_HOME'] = str(root / 'xdg ñ')
    config = Path(env['XDG_CONFIG_HOME']) / 'crsign' / 'crsign.toml'
    config.parent.mkdir(parents=True)
    source = root / 'sample.txt'
    source.write_text('profile signing input')
    env['SIGN_MOCK_DATA'] = str(source)

    def run(*args, program=args_driver, success=True):
        result = subprocess.run([program, *map(str, args)], env=env, cwd=root,
                                capture_output=True, text=True, timeout=10)
        assert (result.returncode == 0) == success, (args, result.stdout, result.stderr)
        return result

    def options(*args):
        return json.loads(run(source, *args).stdout)

    def write(profile='', version='0.1'):
        config.write_text(f'version = {version}\n[[profiles]]\nlabel = "bccr"\n' + profile)

    defaults = options()
    # No selected profile: config is optional and is not read.
    assert 'crsign.toml' in run(source, '-p', 'bccr', success=False).stderr
    config.write_text('this is invalid TOML')
    assert options() == defaults
    run('--help', program=sign)
    write()
    assert options('-p', 'bccr') == defaults
    assert options('--profile=bccr') == defaults
    write(version='"0.1"')
    assert options('-pbccr') == defaults

    # Compare all supported settings to their equivalent CLI arguments.
    write('''name = "Banco ñ"
token = 'pkcs11:token=Mock'
cert_id = "00ff"
algorithm = "rsa-pss"
hash = "sha384"
level = "B-B"
format = "xades"
mime_type = "text/plain"
detached = true
ca = "roots.pem"
intermediates = "chain.pem"
cert_bundles = ["bccr"]
tsa = { url = "https://tsa.example/", ca = "tsa.pem", intermediates = "tsa-chain.pem" }
''')
    profile_cli = ['-t', 'pkcs11:token=Mock', '-k', '00ff', '-a', 'rsa-pss',
                   '-H', 'sha384', '-l', 'B-B', '-f', 'xades',
                   '--mime-type', 'text/plain', '--detached', '--cert-bundle', 'bccr',
                   '--ca-file', config.parent / 'roots.pem',
                   '--intermediates', config.parent / 'chain.pem',
                   '-A', 'https://tsa.example/', '--tsa-ca-file', config.parent / 'tsa.pem',
                   '--tsa-intermediates', config.parent / 'tsa-chain.pem']
    assert options('-p', 'bccr') == options(*profile_cli)
    overrides = ['-t', 'other', '-k', 'abcd', '-a', 'RSA', '-H', 'sha256',
                 '-l', 'B-T', '-f', 'cades', '--mime-type', 'application/custom',
                 '--ca-file', 'cli-roots.pem', '--intermediates', 'cli-chain.pem',
                 '-A', 'https://other.example/', '--tsa-ca-file', 'cli-tsa.pem',
                 '--tsa-intermediates', 'cli-tsa-chain.pem', '--no-bundled-certs']
    expected = options(*overrides, '--detached')
    assert options('-p', 'bccr', *overrides) == expected
    assert options(*overrides, '-p', 'bccr') == expected
    assert options('-p', 'bccr', '--cert-bundle', 'bccr')['cert_bundles'] == ['bccr']
    write('no_bundled_certs = true\n')
    assert options('-p', 'bccr') == options('--no-bundled-certs')
    assert options('-p', 'bccr', '--cert-bundle', 'bccr') == options('--cert-bundle', 'bccr')
    write('no_bundled_certs = false\ndetached = false\ntsa = {}\n')
    assert options('-p', 'bccr') == defaults

    # TOML table syntax, absolute paths, multiple and empty profiles.
    absolute = root / 'absolute.pem'
    write(f'''hash = 'SHA512'
[profiles.tsa]
ca = '{absolute}'
[[profiles]]
label = "empty"
''')
    assert options('-p', 'bccr') == options('-H', 'sha512', '--tsa-ca-file', absolute)
    assert options('-p', 'empty') == defaults
    assert options('-p', 'bccr', '-p', 'empty') == defaults
    assert 'Unknown profile' in run(source, '-p', 'unknown', success=False).stderr
    for command in [('tokens',), ('verify', source), ('--gui',)]:
        run(*command, '-p', 'bccr', success=False)

    # Validation failures identify the selected file and never produce output.
    invalid_profiles = [
        ('hash = "sha1"', 'Unknown value'),
        ('hash = 384', 'Invalid type'),
        ('algorithm = "unknown"', 'Unknown value'),
        ('format = "cms"', 'Use cades'),
        ('level = "B-X"', 'Unknown value'),
        ('token = {}', 'Invalid type'),
        ('token = ""', 'Empty profile field'),
        ('cert_id = 10', 'Invalid type'),
        ('detached = "true"', 'Invalid type'),
        ('hashh = "sha384"', 'Unknown profile field'),
        ('tsa = "url"', 'Invalid type'),
        ('tsa = { cca = "root" }', 'Unknown profile field'),
        ('tsa = { ca = false }', 'Invalid type'),
        ('cert_bundles = [false]', 'must contain strings'),
        ('cert_bundles = ["unknown"]', 'Unknown certificate bundle'),
        ('name = 3', 'Invalid type'),
        ('token = "foo\\u0000bar"', 'Invalid type'),
        ('[[profiles]]\nlabel = "bccr"', 'Duplicate profile label'),
        ('[[profiles]]\nname = "missing label"', 'requires a nonempty'),
    ]
    for body, message in invalid_profiles:
        write(body)
        result = run(source, '-p', 'bccr', success=False)
        assert message in result.stderr and str(config) in result.stderr, result.stderr
    for document in ['version = 0.2', 'version = "1.0"', 'version = true',
                     '[[profiles]]\nlabel = "bccr"', 'version = 0.1\nprofiles = {}',
                     'version = 0.1\nprofiles = [1]', 'version = 0.1\nunknown = true',
                     'version = 0.1\n[[profiles]]\nlabel = "bccr"\nhash = "sha384"\nhash = "sha512"']:
        config.write_text(document)
        result = run(source, '-p', 'bccr', success=False)
        assert str(config) in result.stderr
    write('format = "pades"\ndetached = true\n')
    assert 'PAdES' in run(source, '-p', 'bccr', success=False).stderr
    assert options('-p', 'bccr', '-f', 'cades') == options('--detached', '-f', 'cades')

    # Full operation path: an empty profile preserves token/cert auto-selection.
    write()
    target = root / 'signed.txt'
    def sign_file(*args, success=True):
        if target.exists():
            target.unlink()
        result = run(source, '-p', 'bccr', '-l', 'B-B', '-o', target, '--json',
                     *args, program=sign, success=success)
        if success:
            assert target.read_bytes() == source.read_bytes()
            return json.loads(result.stdout)
        assert not target.exists()
        return result
    assert sign_file()['ok']
    write('level = "B-B"\n')
    target.unlink()
    run(source, '-p', 'bccr', '-o', target, program=sign)
    assert target.read_bytes() == source.read_bytes()
    write('level = "B-B"\ntoken = "wrong"\ncert_id = "abcd"\n')
    assert 'no matching hardware token' in sign_file(success=False).stderr
    assert 'no matching signing certificate' in sign_file('-t', 'pkcs11:token=Mock', success=False).stderr
    assert sign_file('-t', 'pkcs11:token=Mock', '-k', '00ff')['ok']
    # Unspecified format still auto-detects PDFs; a profile can override it.
    source.write_text('%PDF-test')
    write()
    assert sign_file()['format'] == 'pades'
    write('format = "cades"\n')
    assert sign_file()['format'] == 'cades'

    # Both unset and empty XDG_CONFIG_HOME fall back to HOME/.config.
    fallback = Path(env['HOME']) / '.config' / 'crsign' / 'crsign.toml'
    fallback.parent.mkdir(parents=True)
    fallback.write_text('version = 0.1\n[[profiles]]\nlabel = "home"\nhash = "sha512"\n')
    env.pop('XDG_CONFIG_HOME')
    assert options('-p', 'home') == options('-H', 'sha512')
    env['XDG_CONFIG_HOME'] = ''
    assert options('-p', 'home') == options('-H', 'sha512')
print('Profile configuration and CLI precedence checks passed')
