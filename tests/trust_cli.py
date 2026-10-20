# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline integration: system/custom trust, HTTP TSA and failure preservation."""
import http.server
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

sign, driver, openssl = sys.argv[1:]

with tempfile.TemporaryDirectory(prefix='sign-trust-') as directory:
    root = Path(directory)
    env = os.environ.copy()
    env['SSL_CERT_DIR'] = str(root / 'empty-store')
    env['NO_PROXY'] = env['no_proxy'] = '127.0.0.1'

    def run(*args, success=True):
        result = subprocess.run([str(a) for a in args], cwd=root, env=env,
                                capture_output=True, text=True, timeout=60)
        if (result.returncode == 0) != success:
            raise AssertionError(f'{args}: {result.stdout}\n{result.stderr}')
        return result

    def crypto(*args):
        return run(openssl, *args)

    def authority(name):
        crypto('req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256',
               '-days', '2', '-subj', f'/CN=Test {name}',
               '-addext', 'basicConstraints=critical,CA:TRUE',
               '-addext', 'keyUsage=critical,keyCertSign,cRLSign',
               '-keyout', f'{name}.key', '-out', f'{name}.pem')

    def certificate(name, issuer, extensions):
        crypto('req', '-new', '-newkey', 'rsa:2048', '-nodes',
               '-subj', f'/CN=Test {name}', '-keyout', f'{name}.key',
               '-out', f'{name}.csr')
        (root / f'{name}.ext').write_text(extensions)
        crypto('x509', '-req', '-in', f'{name}.csr', '-CA', f'{issuer}.pem',
               '-CAkey', f'{issuer}.key', '-CAcreateserial', '-days', '2',
               '-sha256', '-extfile', f'{name}.ext', '-out', f'{name}.pem')

    authority('document-root')
    authority('tsa-root')
    certificate('signer', 'document-root',
                'basicConstraints=critical,CA:FALSE\nkeyUsage=digitalSignature,nonRepudiation\n')
    certificate('tsa-inter', 'tsa-root',
                'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n')
    certificate('tsa', 'tsa-inter',
                'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n'
                'extendedKeyUsage=critical,timeStamping\n')
    (root / 'input.txt').write_text('Synthetic Sign integration test\n')
    crypto('cms', '-sign', '-binary', '-nodetach', '-cades', '-md', 'sha256',
           '-in', 'input.txt', '-signer', 'signer.pem', '-inkey', 'signer.key',
           '-outform', 'DER', '-out', 'input.p7m')
    env['SSL_CERT_FILE'] = str(root / 'document-root.pem')
    run(sign, '--verify', 'input.p7m')
    env['SSL_CERT_FILE'] = str(root / 'tsa-root.pem')
    run(sign, '--verify', 'input.p7m', success=False)
    run(sign, '--verify', '--ca-file', 'document-root.pem', 'input.p7m')
    run(sign, '--verify', '--ca-file', 'missing.pem', 'input.p7m', success=False)
    run(sign, '--tsa-system-trust', success=False)
    run(sign, '--verify', '-A', 'http://127.0.0.1/', 'input.p7m', success=False)
    run(sign, 'input.p7m', success=False)  # Output must be explicit.

    (root / 'serial').write_text('01\n')
    (root / 'tsa.cnf').write_text(f'''
[tsa]
default_tsa = service
[service]
serial = {root}/serial
signer_cert = {root}/tsa.pem
signer_key = {root}/tsa.key
signer_digest = sha256
default_policy = 1.2.3.4.1
digests = sha256,sha384,sha512
accuracy = secs:1
ordering = no
tsa_name = yes
ess_cert_id_chain = no
ess_cert_id_alg = sha256
''')
    errors = []
    class Handler(http.server.BaseHTTPRequestHandler):
        mode = 'valid'
        def log_message(self, *_):
            pass
        def do_POST(self):
            try:
                assert self.headers['Content-Type'] == 'application/timestamp-query'
                query = self.rfile.read(int(self.headers['Content-Length']))
                (root / 'request.tsq').write_bytes(query)
                crypto('ts', '-reply', '-config', 'tsa.cnf', '-queryfile',
                       'request.tsq', '-out', 'response.tsr')
                reply = (root / 'response.tsr').read_bytes()
                if self.mode == 'corrupt':
                    reply = b'not a timestamp'
                elif self.mode == 'oversize':
                    reply = b'x' * (1024 * 1024 + 1)
                self.send_response(503 if self.mode == 'unavailable' else 200)
                self.send_header('Content-Type', 'application/timestamp-reply')
                self.send_header('Content-Length', str(len(reply)))
                self.end_headers()
                try:
                    self.wfile.write(reply)
                except (BrokenPipeError, ConnectionResetError):
                    pass
            except Exception as error:
                errors.append(error)
                self.close_connection = True

    server = http.server.HTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = f'http://127.0.0.1:{server.server_port}/tsa'
    try:
        # Response omits intermediates; root alone must fail.
        run(driver, 'input.p7m', 'missing-inter.p7m', url, success=False)
        assert not (root / 'missing-inter.p7m').exists()
        run(driver, 'input.p7m', 'stamped.p7m', url, 'tsa-root.pem', 'tsa-inter.pem')
        # Custom TSA root adds to (rather than replaces) the system document root.
        env['SSL_CERT_FILE'] = str(root / 'document-root.pem')
        run(driver, 'input.p7m', 'custom-stamped.p7m', url, 'tsa-root.pem', 'tsa-inter.pem')
        run(sign, '--verify', '--tsa-ca-file', 'tsa-root.pem',
            '--tsa-intermediates', 'tsa-inter.pem', 'custom-stamped.p7m')
        # Check system TSA trust without supplying a TSA CA file.
        env['SSL_CERT_FILE'] = str(root / 'tsa-root.pem')
        run(sign, '--verify', '--ca-file', 'document-root.pem',
            '--tsa-intermediates', 'tsa-inter.pem', 'stamped.p7m')
        run(sign, '--verify', '--ca-file', 'document-root.pem', 'stamped.p7m', success=False)
        for mode in ('corrupt', 'oversize', 'unavailable'):
            Handler.mode = mode
            output = f'{mode}.p7m'
            run(driver, 'input.p7m', output, url, 'tsa-root.pem', 'tsa-inter.pem', success=False)
            assert not (root / output).exists()
        Handler.mode = 'valid'
        before = (root / 'stamped.p7m').read_bytes()
        run(driver, 'input.p7m', 'stamped.p7m', url, 'tsa-root.pem', 'tsa-inter.pem', success=False)
        assert before == (root / 'stamped.p7m').read_bytes()
        assert not errors, errors
    finally:
        server.shutdown()
        thread.join()
        server.server_close()
print('System trust and TSA integration checks passed')
