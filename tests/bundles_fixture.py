# SPDX-License-Identifier: GPL-3.0-or-later
"""Build-only synthetic hierarchies. No personal certificates or network."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

openssl, embed, output, directory = sys.argv[1:]
root = Path(directory)
root.mkdir(parents=True, exist_ok=True)


def crypto(*args):
    subprocess.run([openssl, *args], cwd=root, check=True, capture_output=True)


def issue(name, cn, issuer=None, ca=False, tsa=False):
    crypto('genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:prime256v1', '-out', name + '.key')
    crypto('req', '-new', '-key', name + '.key', '-subj', '/CN=' + cn, '-out', name + '.csr')
    ext = 'basicConstraints=critical,CA:' + ('TRUE' if ca else 'FALSE') + '\n'
    ext += 'keyUsage=critical,' + ('keyCertSign,cRLSign' if ca else 'digitalSignature') + '\n'
    ext += 'subjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid:always\n'
    if tsa:
        ext += 'extendedKeyUsage=critical,timeStamping\n'
    (root / (name + '.ext')).write_text(ext)
    args = ['x509', '-req', '-in', name + '.csr', '-days', '30', '-set_serial', str(len(list(root.glob('*.pem'))) + 1),
            '-extfile', name + '.ext', '-out', name + '.pem']
    args += ['-CA', issuer + '.pem', '-CAkey', issuer + '.key'] if issuer else ['-signkey', name + '.key']
    crypto(*args)


manifests = []
for bundle in ['alpha', 'beta']:
    # Both hierarchies deliberately share issuer DNs, but have different keys.
    issue(bundle + '-root', 'Same root name', ca=True)
    issue(bundle + '-inter', 'Same intermediate name', issuer=bundle + '-root', ca=True)
    issue(bundle + '-signer', bundle + ' signer', issuer=bundle + '-inter')
    issue(bundle + '-tsa', bundle + ' TSA', issuer=bundle + '-inter', tsa=True)
    manifest = {'schema_version': 1, 'id': bundle, 'certificates': []}
    for suffix, role in [('root', 'root'), ('inter', 'intermediate')]:
        name = bundle + '-' + suffix
        der = subprocess.check_output([openssl, 'x509', '-in', str(root / (name + '.pem')), '-outform', 'DER'])
        manifest['certificates'].append({'file': name + '.pem', 'sha256': hashlib.sha256(der).hexdigest(),
                                         'role': role, 'usages': ['document', 'tsa'], 'source': 'synthetic test fixture'})
    path = root / (bundle + '.json')
    path.write_text(json.dumps(manifest))
    manifests.append(str(path))
# Same key and issuer identifiers, but expired: fallback must not bypass dates.
crypto('x509', '-in', 'alpha-root.pem', '-signkey', 'alpha-root.key', '-days', '0', '-out', 'expired-root.pem')
expired = json.loads((root / 'alpha.json').read_text())
expired['id'] = 'expired'
expired['certificates'][0]['file'] = 'expired-root.pem'
der = subprocess.check_output([openssl, 'x509', '-in', str(root / 'expired-root.pem'), '-outform', 'DER'])
expired['certificates'][0]['sha256'] = hashlib.sha256(der).hexdigest()
(root / 'expired.json').write_text(json.dumps(expired))
manifests.append(str(root / 'expired.json'))
subprocess.run([sys.executable, embed, output, *manifests], check=True)
