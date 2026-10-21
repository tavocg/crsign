# SPDX-License-Identifier: GPL-3.0-or-later
"""Audit the shipped public catalogue and exercise generator rejection paths."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

source, openssl = Path(sys.argv[1]), sys.argv[2]
spec = importlib.util.spec_from_file_location('embed', source / 'sign/certs/embed.py')
embed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed)
paths = sorted((source / 'sign/certs').glob('*/manifest.json'))
assert paths
assert embed.catalogue(paths, openssl) == embed.catalogue(paths, openssl)
with tempfile.TemporaryDirectory(prefix='sign-catalog-') as directory:
    temp = Path(directory)
    for path in paths:
        manifest = json.loads(path.read_text())
        assert len(manifest['certificates']) <= 1024
        roots, intermediates = [], []
        for cert in manifest['certificates']:
            original = path.parent / cert['file']
            der, normalized = embed.certificate_bytes(original, openssl)
            if cert.get('file_sha256'):
                assert hashlib.sha256(original.read_bytes()).hexdigest() == cert['file_sha256']
            pem = temp / f'certificate-{len(roots) + len(intermediates)}.pem'
            pem.write_bytes(normalized)
            assert hashlib.sha256(der).hexdigest() == cert['sha256']
            if cert.get('published_sha1'):
                assert hashlib.sha1(der).hexdigest() == cert['published_sha1']
                serial = subprocess.check_output([openssl, 'x509', '-in', str(pem), '-noout', '-serial'], text=True).strip().split('=')[1].lower()
                assert serial == cert['published_serial']
                assert cert['publisher_reference'].startswith('https://')
            details = subprocess.check_output([openssl, 'x509', '-in', str(pem), '-noout', '-ext', 'basicConstraints'], text=True)
            assert 'CA:TRUE' in details, cert
            (roots if cert['role'] == 'root' else intermediates).append(pem)
        (temp / 'roots.pem').write_bytes(b''.join(p.read_bytes() for p in roots))
        (temp / 'intermediates.pem').write_bytes(b''.join(p.read_bytes() for p in intermediates))
        # Check signatures and CA constraints offline; historical generations may be expired.
        for pem in roots + intermediates:
            result = subprocess.run([openssl, 'verify', '-no_check_time', '-no-CAfile', '-no-CApath', '-no-CAstore',
                                     '-CAfile', str(temp / 'roots.pem'), '-untrusted', str(temp / 'intermediates.pem'), str(pem)], capture_output=True, text=True)
            assert result.returncode == 0, (pem, result.stdout, result.stderr)

    sample = json.loads(paths[0].read_text())
    sample['certificates'] = [copy.deepcopy(sample['certificates'][0])]
    pem = (paths[0].parent / sample['certificates'][0]['file']).read_bytes()
    (temp / 'root.pem').write_bytes(pem)
    sample['certificates'][0]['file'] = 'root.pem'
    manifest = temp / 'manifest.json'

    def rejected(value):
        manifest.write_text(json.dumps(value))
        try:
            embed.catalogue([manifest], openssl)
        except (ValueError, KeyError):
            return
        raise AssertionError('invalid catalogue accepted')

    # Encoding and vendor filenames must not change the embedded catalogue.
    manifest.write_text(json.dumps(sample))
    baseline = embed.catalogue([manifest], openssl)
    der, normalized = embed.certificate_bytes(temp / 'root.pem', openssl)
    for name, data in [('Original certificate (1).crt', der), ('certificate.cer', normalized),
                       ('Vendor Certificate.crt', normalized.replace(b'\n', b'\r\n'))]:
        original = temp / name
        original.write_bytes(data)
        assert embed.certificate_bytes(original, openssl) == (der, normalized)
        assert original.read_bytes() == data
        variant = copy.deepcopy(sample)
        variant['certificates'][0]['file'] = name
        variant['certificates'][0]['file_sha256'] = hashlib.sha256(data).hexdigest()
        manifest.write_text(json.dumps(variant))
        assert embed.catalogue([manifest], openssl) == baseline
    for data in [der + b'\x00', normalized + normalized, b'not a certificate']:
        original = temp / 'invalid.crt'
        original.write_bytes(data)
        try:
            embed.certificate_bytes(original, openssl)
        except ValueError:
            pass
        else:
            raise AssertionError('malformed or multiple certificates accepted')
    broken = copy.deepcopy(sample)
    broken['certificates'][0]['file_sha256'] = '0' * 64
    rejected(broken)
    broken = copy.deepcopy(sample)
    broken['certificates'][0]['sha256'] = '0' * 64
    rejected(broken)
    broken = copy.deepcopy(sample)
    broken['certificates'].append(copy.deepcopy(broken['certificates'][0]))
    rejected(broken)
    broken = copy.deepcopy(sample)
    broken['certificates'][0]['role'] = 'leaf'
    rejected(broken)
    broken = copy.deepcopy(sample)
    broken['certificates'][0]['usages'] = ['unknown']
    rejected(broken)
    broken = copy.deepcopy(sample)
    broken['certificates'][0]['file'] = '../outside.pem'
    rejected(broken)
    broken = copy.deepcopy(sample)
    broken['id'] = 'bad name'
    rejected(broken)
    manifest.write_text(json.dumps(sample))
    try:
        embed.catalogue([manifest, manifest], openssl)
    except ValueError:
        pass
    else:
        raise AssertionError('duplicate bundle accepted')
