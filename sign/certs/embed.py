# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a deterministic C catalogue from reviewed manifests; no network."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import urlsplit


def certificate_bytes(path, openssl='openssl', file_sha256=None):
    """Read one original DER/PEM certificate without modifying the source."""
    with path.open('rb') as stream:
        original = stream.read(65537)
    if not original or len(original) > 65536:
        raise ValueError(f'certificate empty or too large: {path}')
    if file_sha256 and hashlib.sha256(original).hexdigest() != file_sha256:
        raise ValueError(f'original certificate bytes changed: {path}')
    if original.lstrip().startswith(b'-----'):
        match = re.fullmatch(rb'\s*-----BEGIN CERTIFICATE-----\s+([A-Za-z0-9+/=\s]+)-----END CERTIFICATE-----\s*', original)
        if not match:
            raise ValueError(f'expected one PEM certificate: {path}')
        der = base64.b64decode(re.sub(rb'\s', b'', match[1]), validate=True)
    else:
        der = original
    checked = subprocess.run([openssl, 'x509', '-inform', 'DER', '-outform', 'DER'],
                             input=der, capture_output=True)
    if checked.returncode or checked.stdout != der:
        raise ValueError(f'expected one DER certificate without trailing bytes: {path}')
    encoded = base64.b64encode(der)
    pem = b'-----BEGIN CERTIFICATE-----\n'
    pem += b''.join(encoded[i:i + 64] + b'\n' for i in range(0, len(encoded), 64))
    pem += b'-----END CERTIFICATE-----\n'
    return der, pem


def catalogue(manifests, openssl='openssl'):
    declarations, entries, seen = [], [], set()
    for manifest_path in manifests:
        path = Path(manifest_path)
        manifest = json.loads(path.read_text())
        name = manifest['id']
        if manifest['schema_version'] != 1 or not re.fullmatch(r'[a-z][a-z0-9_-]*', name) or name in seen:
            raise ValueError(f'invalid or duplicate bundle: {path}')
        seen.add(name)
        tsa_url = manifest.get('tsa_url')
        if tsa_url is not None:
            url = urlsplit(tsa_url)
            if (url.scheme not in ('http', 'https') or not url.hostname or url.username
                    or url.password or url.fragment or len(tsa_url) > 2048
                    or any(ord(c) <= 32 or ord(c) >= 127 for c in tsa_url)):
                raise ValueError(f'invalid TSA URL: {path}')
            _ = url.port
        fingerprints = set()
        if not manifest['certificates']:
            raise ValueError(f'empty bundle: {path}')
        for cert in manifest['certificates']:
            relative = Path(cert['file'])
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError(f'certificate escapes bundle: {relative}')
            certificate_path = (path.parent / relative).resolve()
            if path.parent.resolve() not in certificate_path.parents:
                raise ValueError(f'certificate escapes bundle: {relative}')
            der, pem = certificate_bytes(certificate_path, openssl, cert.get('file_sha256'))
            fingerprint = hashlib.sha256(der).hexdigest()
            if fingerprint != cert['sha256'] or fingerprint in fingerprints:
                raise ValueError(f'duplicate or changed certificate: {relative}')
            fingerprints.add(fingerprint)
            if cert['role'] not in ('root', 'intermediate') or not cert['source']:
                raise ValueError(f'invalid certificate role or origin: {relative}')
            usage = 0
            for item in cert['usages']:
                usage |= {'document': 1, 'tsa': 2}[item]
            if not usage:
                raise ValueError(f'missing usage: {relative}')
            index = len(entries)
            # Byte arrays avoid C string length limits for large RSA certificates.
            declarations.append(f'static const unsigned char pem_{index}[] = {{' + ','.join(map(str, pem)) + '};')
            entries.append('{' + f'{json.dumps(name)}, {json.dumps(fingerprint)}, pem_{index}, sizeof(pem_{index}), {usage}, ' + ('true' if cert['role'] == 'root' else 'false') + ', ' + (json.dumps(tsa_url) if tsa_url else 'NULL') + '}')
    if not entries or len(entries) > 1024:
        raise ValueError('catalogue must contain 1..1024 certificates')
    return '\n'.join(['/* Generated; edit manifests and certificates instead. */', '#include <sign/certs/certs.h>', *declarations,
                      'const sign_cert sign_certs[] = {' + ',\n'.join(entries) + '};',
                      'const size_t sign_certs_count = sizeof(sign_certs) / sizeof(sign_certs[0]);', ''])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openssl', default='openssl')
    parser.add_argument('output', type=Path)
    parser.add_argument('manifests', nargs='+')
    args = parser.parse_args()
    args.output.write_text(catalogue(args.manifests, args.openssl))
