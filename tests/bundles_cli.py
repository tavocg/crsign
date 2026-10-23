# SPDX-License-Identifier: GPL-3.0-or-later
"""Real CLI, synthetic embedded hierarchies and equal issuer names."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from unittest.mock import patch

sign, driver, openssl, fixtures = sys.argv[1:]
fixtures = Path(fixtures)
with tempfile.TemporaryDirectory(prefix='sign-bundles-') as directory:
    root = Path(directory)
    env = os.environ.copy()
    env['XDG_CONFIG_HOME'] = str(root / 'config')
    env.pop('NIX_SSL_CERT_FILE', None)
    env['SSL_CERT_FILE'] = str(root / 'missing-root.pem')
    env['SSL_CERT_DIR'] = str(root / 'missing-directory')
    env['NO_PROXY'] = env['no_proxy'] = '127.0.0.1'

    def run(*args, ok=True):
        result = subprocess.run(list(map(str, args)), cwd=root, env=env, capture_output=True, text=True, timeout=30)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    def verify(*args, ok=True):
        result = run(sign, 'verify', '--json', root / 'signed.p7m', *args, ok=ok)
        return json.loads(result.stdout)

    (root / 'input.txt').write_text('Synthetic bundle selection\n')
    for bundle in ['alpha', 'beta']:
        run(openssl, 'cms', '-sign', '-binary', '-nodetach', '-cades', '-md', 'sha256',
            '-in', root / 'input.txt', '-signer', fixtures / f'{bundle}-signer.pem',
            '-inkey', fixtures / f'{bundle}-signer.key', '-outform', 'DER', '-out', root / 'signed.p7m')
        anchor = verify()['signers'][0]['anchor']
        assert anchor['source'] == 'bundled' and anchor['bundle'] == bundle and len(anchor['sha256']) == 64
        assert verify('--cert-bundle', bundle)['ok']
        if bundle == 'alpha':
            expired = verify('--cert-bundle', 'expired', ok=False)
            assert not expired['trusted'] and expired['signers'][0]['trust_error'] == 10, expired
        assert verify('--cert-bundle', 'alpha', '--cert-bundle', 'beta')['ok']
        other = 'beta' if bundle == 'alpha' else 'alpha'
        assert not verify('--cert-bundle', other, ok=False)['ok']
        assert not verify('--no-bundled-certs', ok=False)['ok']
        # Intermediates alone never establish trust.
        assert not verify('--no-bundled-certs', '--intermediates', fixtures / f'{bundle}-inter.pem', ok=False)['ok']
        env['SSL_CERT_FILE'] = str(fixtures / f'{bundle}-root.pem')
        anchor = verify()['signers'][0]['anchor']
        assert anchor['source'] == 'system' and anchor['bundle'] is None
        env['SSL_CERT_FILE'] = str(root / 'missing-root.pem')
        anchor = verify('--ca-file', fixtures / f'{bundle}-root.pem')['signers'][0]['anchor']
        assert anchor['source'] == 'additional' and anchor['bundle'] is None
        # A different primary root with the same DN must not hide the actual issuer.
        assert verify('--ca-file', fixtures / f'{other}-root.pem')['signers'][0]['anchor']['bundle'] == bundle
        # DER user intermediates can coexist with embedded PEMs.
        run(openssl, 'x509', '-in', fixtures / f'{bundle}-inter.pem', '-outform', 'DER', '-out', 'inter.der')
        assert verify('--intermediates', root / 'inter.der')['ok']

    run(sign, 'verify', '--cert-bundle', 'unknown', root / 'signed.p7m', ok=False)
    run(sign, 'verify', '--cert-bundle', 'alpha', '--no-bundled-certs', root / 'signed.p7m', ok=False)
    run(sign, 'tokens', '--cert-bundle', 'alpha', ok=False)
    run(sign, 'tokens', '--no-bundled-certs', ok=False)

    # Applying a TSA response uses the same fallback without another HTTP request.
    (root / 'serial').write_text('01\n')
    (root / 'tsa.cnf').write_text(f'''
[tsa]
default_tsa = service
[service]
serial = {root}/serial
signer_cert = {fixtures}/beta-tsa.pem
signer_key = {fixtures}/beta-tsa.key
signer_digest = sha256
default_policy = 1.2.3.4.1
digests = sha256,sha384,sha512
accuracy = secs:1
ordering = no
tsa_name = yes
ess_cert_id_chain = no
''')
    calls = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            calls.append(self.path)
            (root / 'request.tsq').write_bytes(self.rfile.read(int(self.headers['Content-Length'])))
            run(openssl, 'ts', '-reply', '-config', root / 'tsa.cnf', '-queryfile', root / 'request.tsq', '-out', root / 'response.tsr')
            body = (root / 'response.tsr').read_bytes()
            self.send_response(200)
            self.send_header('Content-Type', 'application/timestamp-reply')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    with patch('socket.getfqdn', return_value='localhost'):
        server = http.server.HTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        run(driver, root / 'signed.p7m', root / 'timestamped.p7m', f'http://127.0.0.1:{server.server_port}/tsa')
        assert calls == ['/tsa'], calls
        result = json.loads(run(sign, 'verify', '--json', root / 'timestamped.p7m').stdout)
        assert result['ok'] and result['signers'][0]['timestamp']['anchor']['bundle'] == 'beta', result
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
