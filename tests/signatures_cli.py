# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real Sign verifier with libssig's synthetic conformance fixtures."""
import base64
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sign, ades_dir, document_dir = sys.argv[1:]
ades, documents = Path(ades_dir), Path(document_dir)
levels = ['B-B', 'B-T', 'B-LT', 'B-LTA']


def verify(path, ca, tsa, success=True, structured=True):
    args = [sign, 'verify', str(path), '--ca-file', str(ca), '--tsa-ca-file', str(tsa)]
    if structured:
        args.append('--json')
    result = subprocess.run(args, capture_output=True, text=True, timeout=30)
    assert (result.returncode == 0) == success, (args, result.stdout, result.stderr)
    if structured:
        value = json.loads(result.stdout)
        assert value['command'] == 'verify' and value['ok'] == success
        return value, result.stderr
    return result.stdout, result.stderr


def valid(path, ca, tsa, fmt, level):
    value, _ = verify(path, ca, tsa)
    assert value['signature_valid'] and value['trusted'] and value['trust_checked'], value
    assert value['format'] == fmt and value['count'] == 1 and not value['detached'], value
    signer = value['signers'][0]
    assert signer['level'] == level and signer['level_validated'], signer
    assert signer['timestamp']['present'] == (level != 'B-B'), signer
    assert signer['archive']['present'] == (level == 'B-LTA'), signer
    if level == 'B-LTA':
        assert signer['history'] == 'valid', signer


cases = []
for i, level in enumerate(levels):
    for fmt, name in [('cades', f'ades-level-{i}.p7m'), ('pades', f'pades-level-{i}.pdf')]:
        cases.append((ades / name, ades / 'cades-0-0.pem', ades / 'ades-tsa-root.pem', fmt, level))
    for fmt, code, ext in [('jades', 0, 'json'), ('xades', 1, 'xml')]:
        path = documents / f'document-0-0-{code}-0-{i}.{ext}'
        cases.append((path, documents / 'document-tsa-root.pem', documents / 'document-tsa-root.pem', fmt, level))
for fmt, name in [('cades', 'ades-renewed.p7m'), ('pades', 'pades-renewed.pdf')]:
    cases.append((ades / name, ades / 'cades-0-0.pem', ades / 'ades-tsa-root.pem', fmt, 'B-LTA'))
for case in cases:
    valid(*case)
    # Text and JSON must report the same outcome on the real CLI.
    verify(*case[:3], structured=False)

# Sign currently has no argument for supplying detached content: require a
# useful failure, never report successful verification without those bytes.
for code, ext in [(0, 'json'), (1, 'xml')]:
    path = documents / f'document-0-0-{code}-1-3.{ext}'
    value, stderr = verify(path, documents / 'document-tsa-root.pem', documents / 'document-tsa-root.pem', success=False)
    assert not value['signature_valid'] and 'original content' in stderr, (value, stderr)

with tempfile.TemporaryDirectory(prefix='sign-conformance-') as temp:
    root = Path(temp)
    for fmt, code, ext in [('jades', 0, 'json'), ('xades', 1, 'xml')]:
        source = documents / f'document-0-0-{code}-0-3.{ext}'
        original = source.read_bytes()
        if fmt == 'jades':
            parsed = json.loads(original)
            parsed['payload'] = base64.urlsafe_b64encode(b'tampered').decode().rstrip('=')
            changed = json.dumps(parsed).encode()
        else:
            content = ET.fromstring(original).find('{http://www.w3.org/2000/09/xmldsig#}Object[@Id="Content"]').text.encode()
            assert original.count(content) == 1
            changed = original.replace(content, base64.b64encode(b'tampered'), 1)
        path = root / f'changed.{ext}'
        path.write_bytes(changed)
        value, _ = verify(path, documents / 'document-tsa-root.pem', documents / 'document-tsa-root.pem', success=False)
        assert not value['signature_valid'], value

        # Strip LT evidence while preserving the document's signed bytes.
        if fmt == 'jades':
            parsed = json.loads(original)
            items = parsed['header']['etsiU']
            parsed['header']['etsiU'] = [item for item in items if 'anyValData' not in json.loads(base64.urlsafe_b64decode(item + '=' * (-len(item) % 4)))]
            assert len(parsed['header']['etsiU']) < len(items)
            changed = json.dumps(parsed).encode()
        else:
            changed, count = re.subn(rb'<xades141:AnyValidationData\b[^>]*>.*?</xades141:AnyValidationData>', b'', original, flags=re.DOTALL)
            assert count == 1
        path.write_bytes(changed)
        value, _ = verify(path, documents / 'document-tsa-root.pem', documents / 'document-tsa-root.pem', success=False)
        assert value['signature_valid'] and not value['signers'][0]['level_validated'], value

    for path, ca, tsa, fmt, level in cases:
        if level == 'B-B':
            value, _ = verify(path, documents / 'document-tsa-root.pem' if fmt in ['cades', 'pades'] else ades / 'cades-0-0.pem', tsa, success=False)
            assert value['signature_valid'] and not value['trusted'], value

print('Real Sign CLI conformance fixtures passed: four formats/levels, renewal and negative cases')
