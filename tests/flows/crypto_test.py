# SPDX-License-Identifier: GPL-3.0-or-later
"""Real CMS, XML, JSON and PDF signatures produced through JSON flows."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

flow_driver, sign = sys.argv[1:]

def pdf():
    objects = [b'<< /Type /Catalog /Pages 2 0 R >>',
               b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
               b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>']
    data = b'%PDF-1.7\n'
    offsets = []
    for i, obj in enumerate(objects, 1):
        offsets.append(len(data))
        data += f'{i} 0 obj\n'.encode() + obj + b'\nendobj\n'
    start = len(data)
    data += b'xref\n0 4\n0000000000 65535 f \n'
    data += b''.join(f'{n:010} 00000 n \n'.encode() for n in offsets)
    return data + f'trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n'.encode()

with tempfile.TemporaryDirectory(prefix='crsign-flow-crypto-') as directory:
    root = Path(directory)
    env = os.environ.copy()
    env['XDG_CONFIG_HOME'] = str(root/'config')
    contents = bytes(range(256)) + b'\0 arbitrary non-PDF bytes\xff'
    (root/'payload.bin').write_bytes(contents)
    (root/'page.pdf').write_bytes(pdf())
    documents = [dict(input='payload.bin', output='binary.p7m', level='B-B', mime_type='application/octet-stream'),
                 dict(input='payload.bin', output='binary.xml', format='xades', level='B-B'),
                 dict(input='payload.bin', output='binary.json', format='jades', level='B-B'),
                 dict(input='page.pdf', output='visible.pdf', level='B-B',
                      appearance=dict(enabled=True, page_number=1, x=36, y=48)),
                 dict(input='page.pdf', output='invisible.pdf', level='B-B', appearance=dict(enabled=False))]
    path = root/'flow.json'
    path.write_text(json.dumps(dict(version=1, documents=documents)))
    result = subprocess.run([flow_driver, 'run', str(path), str(root)], env=env,
                            capture_output=True, timeout=30)
    assert result.returncode == 0, (result.stdout, result.stderr)
    report = json.loads(result.stdout)
    assert report['completed'] == len(documents) and report['pin_requests'] == len(documents)
    # The fixture's self-signed certificate is trusted only by this test invocation.
    inspection = subprocess.run([sign, 'verify', '--json', str(root/'binary.p7m')],
                                env=env, capture_output=True, timeout=10)
    inspected = json.loads(inspection.stdout)
    assert inspected['signature_valid'] and not inspected['trusted'], inspected
    anchor = root/'fixture-anchor.der'
    anchor.write_bytes(bytes.fromhex(inspected['signers'][0]['certificate_der_hex']))
    for doc in documents:
        output = root/doc['output']
        assert output.exists()
        verification = subprocess.run([sign, 'verify', '--json', '--ca-file', str(anchor), str(output)], env=env,
                                      capture_output=True, timeout=10)
        assert verification.returncode == 0, (output, verification.stdout, verification.stderr)
        verified = json.loads(verification.stdout)
        assert verified['signature_valid'], verified
    assert (root/'visible.pdf').read_bytes().startswith(pdf())
    assert b'/Subtype /Image' in (root/'visible.pdf').read_bytes()
    assert b'/Subtype /Image' not in (root/'invisible.pdf').read_bytes()
    for filename in ('payload.bin','page.pdf'):
        assert (root/filename).read_bytes() == (contents if filename == 'payload.bin' else pdf())
print('Real flow signatures verified for arbitrary binary input and PDF')
