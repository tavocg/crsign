# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a deterministic C catalogue from reviewed manifests; no network."""
import base64
import hashlib
import json
from pathlib import Path
import re
import sys


def catalogue(manifests):
    declarations, entries, seen = [], [], set()
    for manifest_path in manifests:
        path = Path(manifest_path)
        manifest = json.loads(path.read_text())
        name = manifest['id']
        if manifest['schema_version'] != 1 or not re.fullmatch(r'[a-z][a-z0-9_-]*', name) or name in seen:
            raise ValueError(f'invalid or duplicate bundle: {path}')
        seen.add(name)
        fingerprints = set()
        if not manifest['certificates']:
            raise ValueError(f'empty bundle: {path}')
        for cert in manifest['certificates']:
            relative = Path(cert['file'])
            if relative.is_absolute() or '..' in relative.parts or relative.suffix != '.pem':
                raise ValueError(f'certificate escapes bundle: {relative}')
            certificate_path = (path.parent / relative).resolve()
            if path.parent.resolve() not in certificate_path.parents:
                raise ValueError(f'certificate escapes bundle: {relative}')
            pem = certificate_path.read_bytes()
            if len(pem) > 65536:
                raise ValueError(f'certificate too large: {relative}')
            match = re.fullmatch(rb'-----BEGIN CERTIFICATE-----\s+([A-Za-z0-9+/=\s]+)-----END CERTIFICATE-----\s*', pem)
            if not match:
                raise ValueError(f'expected one PEM certificate: {relative}')
            der = base64.b64decode(re.sub(rb'\s', b'', match[1]), validate=True)
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
            entries.append('{' + f'{json.dumps(name)}, {json.dumps(fingerprint)}, pem_{index}, sizeof(pem_{index}), {usage}, ' + ('true' if cert['role'] == 'root' else 'false') + '}')
    if not entries or len(entries) > 1024:
        raise ValueError('catalogue must contain 1..1024 certificates')
    return '\n'.join(['/* Generated; edit manifests and certificates instead. */', '#include <sign/certs/certs.h>', *declarations,
                      'const sign_cert sign_certs[] = {' + ',\n'.join(entries) + '};',
                      'const size_t sign_certs_count = sizeof(sign_certs) / sizeof(sign_certs[0]);', ''])


if __name__ == '__main__':
    Path(sys.argv[1]).write_text(catalogue(sys.argv[2:]))
