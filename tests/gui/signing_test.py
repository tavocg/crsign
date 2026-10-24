# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the GTK signing workflow with real signatures and an ephemeral PIN."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def pdf():
    objects = [b'<< /Type /Catalog /Pages 2 0 R >>',
               b'<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>',
               b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>',
               b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 500] /CropBox [20 40 260 440] /Rotate 90 /UserUnit 2 /Resources << >> >>']
    data = b'%PDF-1.7\n'
    offsets = []
    for i, obj in enumerate(objects, 1):
        offsets.append(len(data))
        data += f'{i} 0 obj\n'.encode() + obj + b'\nendobj\n'
    start = len(data)
    data += b'xref\n0 5\n0000000000 65535 f \n'
    data += b''.join(f'{n:010} 00000 n \n'.encode() for n in offsets)
    return data + f'trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n'.encode()


with tempfile.TemporaryDirectory(prefix='crsign-gui-signing-') as directory:
    root = Path(directory)
    config = root/'config'/'crsign'
    config.mkdir(parents=True)
    (config/'crsign.toml').write_text('version=0.1\nlevel="B-B"\n[[profiles]]\nlabel="test"\nname="Test profile"\n')
    (root/'page.pdf').write_bytes(pdf())
    (root/'payload.bin').write_bytes(b'arbitrary document\x00\xff')
    (root/'image.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100"><rect width="200" height="100" fill="green"/></svg>')
    (root/'invalid.svg').write_text('not an image')
    env = dict(os.environ, XDG_CONFIG_HOME=str(root/'config'), XDG_DATA_HOME=str(root/'data'),
               SIGN_FLOW_TEST_ANCHOR=str(root/'anchor.der'))
    subprocess.run([sys.argv[1], str(root), sys.argv[3]], env=env, check=True, timeout=45)
    for name in ('page-signed.pdf', 'payload-signed.bin'):
        result = subprocess.run([sys.argv[2], 'verify', '--json', '--ca-file', str(root/'anchor.der'), str(root/name)],
                                env=env, check=True, capture_output=True, timeout=10)
        import json
        report = json.loads(result.stdout)
        assert report['signature_valid'] and report['trusted'], report
    assert b'/Subtype /Image' in (root/'page-signed.pdf').read_bytes()
    history = json.loads((root/'data'/'crsign'/'history.json').read_text())
    assert {Path(item['path']).name for item in history['documents']} == {'page-signed.pdf','payload-signed.bin'}

    env['SIGN_FLOW_TEST_NO_TOKEN']='1'
    env['SIGN_FLOW_TEST_ANCHOR']=str(root/'unused-anchor.der')
    subprocess.run([sys.argv[1], str(root), sys.argv[3], 'no-token'], env=env, check=True, timeout=20)
print('GTK signing, PDF placement, image formats, PIN dialog and real verification passed')
