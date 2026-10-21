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
assert embed.catalogue(paths) == embed.catalogue(paths)
with tempfile.TemporaryDirectory(prefix='sign-catalog-') as directory:
    temp = Path(directory)
    for path in paths:
        manifest = json.loads(path.read_text())
        assert len(manifest['certificates']) <= 1024
        roots, intermediates = [], []
        for cert in manifest['certificates']:
            pem = path.parent / cert['file']
            der = subprocess.check_output([openssl, 'x509', '-in', str(pem), '-outform', 'DER'])
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
            embed.catalogue([manifest])
        except (ValueError, KeyError):
            return
        raise AssertionError('invalid catalogue accepted')

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
        embed.catalogue([manifest, manifest])
    except ValueError:
        pass
    else:
        raise AssertionError('duplicate bundle accepted')
