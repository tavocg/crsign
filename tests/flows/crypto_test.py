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
    (root/'image.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100"><rect width="200" height="100" fill="red"/></svg>')
    documents = [dict(input='payload.bin', output='binary.p7m', level='B-B', mime_type='application/octet-stream'),
                 dict(input='payload.bin', output='binary.xml', format='xades', level='B-B'),
                 dict(input='payload.bin', output='binary.json', format='jades', level='B-B'),
                 dict(input='page.pdf', output='visible.pdf', level='B-B',
                      appearance=dict(enabled=True, page_number=1, x=36, y=48)),
                 dict(input='page.pdf', output='invisible.pdf', level='B-B', appearance=dict(enabled=False))]
    documents.extend([
        dict(input='page.pdf',output='combined.pdf',level='B-B',
             appearance=dict(enabled=True,page_number=1,x=36,y=48,image='image.svg',information=True)),
        dict(input='page.pdf',output='image-only.pdf',level='B-B',
             appearance=dict(enabled=True,page_number=1,x=36,y=48,image='image.svg',information=False))])
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
    def run_flow(items, ok=True, mode='no-pin'):
        path.write_text(json.dumps(dict(version=1, documents=items)))
        p = subprocess.run([flow_driver, mode, str(path), str(root)], env=env,
                           capture_output=True, timeout=30)
        assert (p.returncode == 0) == ok, (p.stdout, p.stderr)
        return json.loads(p.stdout)

    checks = [dict(operation='verify', input=doc['output'], ca=anchor.name)
              for doc in documents]
    def snapshot():
        # Compare entry names: native Windows enumeration may spell the parent
        # directory differently from tempfile (case or short-path aliases).
        return {entry.name: entry.read_bytes() for entry in root.iterdir()
                if entry.name != path.name}

    before = snapshot()
    verified = run_flow(checks)
    assert verified['completed'] == len(checks) and verified['pin_requests'] == 0
    assert all(r['attempted'] and r['ok'] and r['signature_valid'] and
               r['trust_checked'] and r['trusted'] and r['signers'] == 1
               for r in verified['documents'])
    after = snapshot()
    assert before == after, {name for name in before.keys() | after.keys()
                             if before.get(name) != after.get(name)}
    # Valid cryptography without trust must still fail and preserve the report.
    failed = run_flow([checks[0], dict(operation='verify', input='binary.p7m'), checks[1]], ok=False)
    assert failed['completed'] == 1 and failed['failed_document'] == 1
    assert failed['documents'][1]['signature_valid'] and not failed['documents'][1]['trusted']
    assert failed['documents'][1]['status'] != 0 and failed['documents'][1]['signers'] == 1
    assert not failed['documents'][2]['attempted'] and failed['pin_requests'] == 0
    # Tampering stops validation before any later signing or PIN callback.
    damaged = (root/'binary.p7m').read_bytes().replace(contents, bytes([1]) + contents[1:], 1)
    assert damaged != (root/'binary.p7m').read_bytes()
    (root/'damaged.p7m').write_bytes(damaged)
    failed = run_flow([checks[0], dict(operation='verify', input='damaged.p7m', ca=anchor.name),
                       dict(input='payload.bin', output='never-signed.p7m', level='B-B')], ok=False)
    assert failed['completed'] == 1 and failed['failed_document'] == 1
    assert not failed['documents'][1]['signature_valid'] and not failed['documents'][2]['attempted']
    assert failed['pin_requests'] == 0 and not (root/'never-signed.p7m').exists()
    # Each signing process exports only its synthetic certificate, allowing a
    # complete trusted sign-then-verify flow without sharing private keys.
    env['SIGN_FLOW_TEST_ANCHOR'] = str(root/'runtime-anchor.der')
    mixed = run_flow([dict(input='payload.bin', output='mixed.p7m', level='B-B'),
                      dict(operation='verify', input='mixed.p7m', ca='runtime-anchor.der'),
                      checks[0]], mode='run')
    assert mixed['completed'] == 3 and mixed['pin_requests'] == 1
    assert mixed['documents'][1]['signature_valid'] and mixed['documents'][1]['trusted']
    # Detached signatures require original content, including an empty file.
    for name, data in [('payload.bin', contents), ('empty.bin', b'')]:
        (root/name).write_bytes(data)
        check = dict(operation='verify', input=name+'.p7s', content=name, ca='runtime-anchor.der')
        mixed = run_flow([dict(input=name, output=name+'.p7s', level='B-B', detached=True),
                          check], mode='run')
        assert mixed['completed'] == 2 and mixed['pin_requests'] == 1
        assert run_flow([check])['documents'][0]['signature_valid']
        run_flow([dict(operation='verify', input=name+'.p7s', ca='runtime-anchor.der')], ok=False)
        (root/'wrong.bin').write_bytes(b'wrong')
        run_flow([dict(check, content='wrong.bin')], ok=False)
    # Attached signatures reject an external content file.
    run_flow([dict(checks[0], content='payload.bin')], ok=False)
    assert (root/'visible.pdf').read_bytes().startswith(pdf())
    assert b'/Subtype /Image' in (root/'visible.pdf').read_bytes()
    assert b'/Subtype /Image' not in (root/'invisible.pdf').read_bytes()
    for filename in ('payload.bin','page.pdf'):
        assert (root/filename).read_bytes() == (contents if filename == 'payload.bin' else pdf())
print('Real flow signing, batch verification and mixed flows passed')
