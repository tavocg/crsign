# SPDX-License-Identifier: GPL-3.0-or-later
"""Discovery tests use local chains only; no card or network."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

driver, fixtures, openssl = sys.argv[1:]
fixtures = Path(fixtures)
env = dict(os.environ, SSL_CERT_FILE='/nonexistent', SSL_CERT_DIR='/nonexistent')
env.pop('NIX_SSL_CERT_FILE', None)

def check(cert='alpha-signer.pem', bundle='alpha', mode='auto', tsa='-', ca='-', expected='https://alpha.example/tsa'):
    result = subprocess.run([driver, str(fixtures / cert), bundle, mode, tsa, ca], env=env,
                            capture_output=True, text=True)
    assert (result.returncode == 0) == (expected is not None), result.stderr
    if expected is not None:
        assert result.stdout.strip() == expected, result.stdout

check()
check(bundle='all')
check(cert='beta-signer.pem', bundle='beta', expected='https://beta.example/tsa')
check(bundle='beta', expected=None)  # identical issuer names, wrong keys
check(bundle='none', expected=None)
check(bundle='expired', expected=None)
check(cert='missing.pem', mode='https://explicit.example/tsa', expected='https://explicit.example/tsa')
check(cert='missing.pem', mode='offline', expected='offline')
check(bundle='none', ca=str(fixtures / 'alpha-root.pem'), expected=None)  # no catalogue policy
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    def sia(name, urls, issuer='alpha'):
        ext = tmp / (name + '.ext')
        ext.write_text('basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n'
                       'extendedKeyUsage=critical,timeStamping\nsubjectKeyIdentifier=hash\n'
                       'authorityKeyIdentifier=keyid:always\nsubjectInfoAccess=' +
                       ','.join('1.3.6.1.5.5.7.48.3;URI:' + url for url in urls) + '\n')
        output = tmp / (name + '.pem')
        subprocess.run([openssl, 'x509', '-req', '-in', str(fixtures / (issuer + '-tsa.csr')),
                        '-CA', str(fixtures / (issuer + '-inter.pem')), '-CAkey', str(fixtures / (issuer + '-inter.key')),
                        '-set_serial', '123', '-days', '1', '-extfile', str(ext), '-out', str(output)],
                       capture_output=True, check=True)
        return str(output)
    check(tsa=sia('valid', ['https://sia.example/tsa']), expected='https://sia.example/tsa')
    check(tsa=sia('ambiguous', ['https://one.example/tsa', 'https://two.example/tsa']), expected=None)
    check(tsa=sia('untrusted', ['https://wrong.example/tsa'], 'beta'))
    check(tsa=sia('invalid', ['ftp://invalid.example/tsa']))
