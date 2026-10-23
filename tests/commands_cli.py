# SPDX-License-Identifier: GPL-3.0-or-later
"""Command dispatch/JSON tests with a deterministic hardware API substitute."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sign, openssl = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='sign-commands-') as directory:
    root = Path(directory)
    env = os.environ.copy()
    env['XDG_CONFIG_HOME'] = str(root / 'config')
    env.pop('NIX_SSL_CERT_FILE', None)
    source = root / 'sample.txt'
    source.write_text('test signature bytes')
    env['SIGN_MOCK_DATA'] = str(source)

    def run(*args, success=True, structured=False):
        result = subprocess.run([sign, *(['-l', 'B-B'] if args and str(args[0]) not in ('tokens', 'verify', 't', 'v', '--json', '--gui') and '--help' not in args else []), *map(str, args)], cwd=root, env=env,
                                capture_output=True, text=True, timeout=10)
        assert (result.returncode == 0) == success, (args, result.stdout, result.stderr)
        return json.loads(result.stdout) if structured else result

    def output(*args, success=True):
        return run(*args, '--json', success=success, structured=True)

    missing_tsa = subprocess.run([sign, str(source), '--json'], cwd=root, env=env,
                                 capture_output=True, text=True)
    assert missing_tsa.returncode != 0 and 'B-B' in missing_tsa.stderr
    for level in ['B-B', 'b-b']:
        args = [sign, str(source), '-l', level, '-a', 'RSA', '-H', 'SHA256', '--json']
        target = root / 'sample-signed.txt'
        if target.exists(): target.unlink()
        result = subprocess.run(args, cwd=root, env=env, capture_output=True, text=True)
        assert result.returncode == 0 and json.loads(result.stdout)['level'] == 'B-B'
        target.unlink()
    assert output('t') == output('tokens')
    assert output('--json', 't', '--all') == output('--json', 'tokens', '--all')
    assert run('t').stdout == run('tokens').stdout
    tokens = output('tokens')
    assert tokens['command'] == 'tokens' and tokens['ok']
    assert tokens['count'] == len(tokens['tokens']) == 1
    token = tokens['tokens'][0]
    assert token['label'] == 'Tarjeta ñ "prueba"\n'
    cert = token['certificates'][0]
    assert cert['id_hex'] == '00ff' and cert['label'] == 'Firma\0ñ'
    assert cert['extended_key_usage'] == ['emailProtection', '1.2.3.999']
    assert cert['certificate_policies'] == ['cr-persona-fisica-firma', '1.2.3.998']

    assert 'der_hex' not in cert and 'content' not in cert
    content = output('tokens', '--content')['tokens'][0]['certificates'][0]
    assert content['der_hex'] is None and 'content' not in content
    subprocess.run([openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                    '-days', '1', '-subj', '/CN=Content Test',
                    '-keyout', str(root / 'cert.key'), '-outform', 'DER',
                    '-out', str(root / 'cert.der')], check=True,
                   capture_output=True)
    env['SIGN_MOCK_DER'] = str(root / 'cert.der')
    content = output('tokens', '--all', '--content')['tokens'][0]['certificates']
    assert len(content) == 2
    expected = (root / 'cert.der').read_bytes().hex()
    for item in content:
        assert item['der_hex'] == expected and 'content' not in item
    assert expected in run('tokens', '--content').stdout
    assert expected not in run('tokens').stdout
    summary = output('tokens')['tokens'][0]['certificates'][0]
    assert 'der_hex' not in summary and 'content' not in summary
    del env['SIGN_MOCK_DER']
    all_certs = output('--json', 'tokens', '--all')
    assert len(all_certs['tokens'][0]['certificates']) == 2
    authentication = all_certs['tokens'][0]['certificates'][1]
    assert authentication['extended_key_usage'] == ['clientAuth', 'msSmartcardLogin']
    assert authentication['certificate_policies'] == ['cr-persona-fisica-autenticacion']
    text = subprocess.run([sign, 'tokens', '--all'], cwd=root, env=env,
                          capture_output=True, text=True, errors='replace',
                          check=True).stdout
    for name in ['clientAuth', 'msSmartcardLogin', 'emailProtection',
                 'cr-persona-fisica-firma', 'cr-persona-fisica-autenticacion',
                 '1.2.3.999', '1.2.3.998']:
        assert name in text

    assert all_certs['tokens'][0]['certificates'][1]['label'] == 'xÿ'
    assert '00ff' in run('tokens').stdout
    assert run('tokens', '--help').returncode == 0
    for invalid in [('tokens', 'file'), ('tokens', '-o', 'out'),
                    ('tokens', '-A', 'url'), ('verify',),
                    ('verify', 'file', '-a', 'rsa-pkcs'),
                    ('verify', 'file', '-o', 'out'),
                    ('verify', 'file', '--content'),
                    ('file', '--content'),
                    ('file', '--all', '-o', 'out'),
                    ('file', 'extra', '-o', 'out'), ('--gui', '--json')]:
        run(*invalid, success=False)
    for flag in ['SIGN_MOCK_FAIL', 'SIGN_MOCK_CERT_FAIL']:
        env[flag] = '1'
        assert not output('tokens', success=False)['ok']
        del env[flag]
    env['SIGN_MOCK_EMPTY'] = '1'
    assert output('tokens')['tokens'] == []
    del env['SIGN_MOCK_EMPTY']

    # Keep directory and final extension; leading dots are part of the name.
    for original, expected in [
        ('document.txt', 'document-signed.txt'),
        ('archive.tar.gz', 'archive.tar-signed.gz'),
        ('README', 'README-signed'),
        ('.hidden', '.hidden-signed'),
        ('.hidden.txt', '.hidden-signed.txt'),
        ('folder.with.dots/file', 'folder.with.dots/file-signed'),
        ('subdir/ñ doc.pdf', 'subdir/ñ doc-signed.pdf'),
        ('subdir/ñ \"doc\".pdf', 'subdir/ñ \"doc\"-signed.pdf'),
    ]:
        if os.name == 'nt' and '"' in original:
            continue  # Windows does not permit quotation marks in filenames.
        path = root / original
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'input')
        result = output(original)
        assert result['ok'] and result['output'] == expected, result
        destination = root / expected
        assert destination.read_bytes() == source.read_bytes()
        before = destination.read_bytes()
        assert not output(original, success=False)['ok']
        assert destination.read_bytes() == before

    for fmt in ['cades', 'pades', 'xades', 'jades']:
        signed_format = output(source, '-f', fmt, '-o', f'format-{fmt}.signed')
        assert signed_format['ok'] and signed_format['format'] == fmt

    destination = root / 'signed "ñ".p7m'
    signed = output(source, '-o', destination)
    assert signed['command'] == 'sign' and signed['ok']
    assert signed['output'] == str(destination) and signed['len'] == source.stat().st_size
    assert not signed['detached'] and not signed['timestamped']
    assert destination.read_bytes() == source.read_bytes()
    assert not output(source, '-o', destination, success=False)['ok']
    assert destination.read_bytes() == source.read_bytes()
    assert not output('missing', '-o', 'missing.p7m', success=False)['ok']
    assert not output('verify', 'missing.p7m', success=False)['ok']
    invalid = output('verify', source, success=False)
    assert invalid['command'] == 'verify' and not invalid['signature_valid']
    assert output('v', source, success=False) == invalid
    assert output('--json', 'v', source, success=False) == invalid
    assert run('v', source, success=False).stdout == run('verify', source, success=False).stdout
    for name in ('tokens', 'verify'):
        (root / name).write_bytes(b'input')
        assert output('./' + name, '-o', name + '.p7m')['command'] == 'sign'
    env['SIGN_MOCK_SIGN_FAIL'] = '1'
    assert not output(source, '-o', 'failure.p7m', success=False)['ok']
    assert not (root / 'failure.p7m').exists()
print('Command dispatch and JSON checks passed')
