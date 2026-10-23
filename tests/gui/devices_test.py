# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise hardware monitoring and both system and embedded trust offline."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, module, bundles, snapshots, openssl = sys.argv[1:]
bundles = Path(bundles).resolve()
with tempfile.TemporaryDirectory(prefix='crsign-gui-devices-') as directory:
    root = Path(directory)
    env = os.environ.copy()
    env['XDG_CONFIG_HOME'] = str(root/'config')
    env['LIBSSIG_DEVICE_CERT'] = str(root/'signer.der')
    env['LIBSSIG_DEVICE_PHASE'] = str(root/'phase')
    env['LIBSSIG_DEVICE_LOG'] = str(root/'module.log')
    env['SSL_CERT_FILE'] = str(bundles/'alpha-root.pem')
    env['SSL_CERT_DIR'] = str(root/'empty-store')
    (root/'empty-store').mkdir()
    (root/'phase').write_text('0')

    def crypto(*args):
        subprocess.run([openssl, *map(str, args)], cwd=root, check=True,
                       capture_output=True, timeout=15)

    crypto('req', '-new', '-newkey', 'rsa:2048', '-nodes', '-keyout', 'signer.key',
           '-subj', '/CN=Device fixture signer', '-out', 'signer.csr')
    (root/'signer.ext').write_text('basicConstraints=critical,CA:FALSE\n'
                                  'keyUsage=critical,digitalSignature,nonRepudiation\n')
    crypto('x509', '-req', '-in', 'signer.csr', '-CA', bundles/'alpha-inter.pem',
           '-CAkey', bundles/'alpha-inter.key', '-set_serial', '901', '-days', '2',
           '-extfile', 'signer.ext', '-out', 'signer.pem')
    crypto('x509', '-in', 'signer.pem', '-outform', 'DER', '-out', 'signer.der')
    config = root/'config'/'crsign'/'crsign.toml'
    config.parent.mkdir(parents=True)
    config.write_text('version=0.1\nmodules=' + json.dumps([str(Path(module).resolve())]) +
                      '\ncert_bundles=["alpha"]\n')
    subprocess.run([binary, str(root), str(Path(snapshots).resolve())], env=env,
                   check=True, timeout=40)
    log = (root/'module.log').read_text()
    assert log.count('initialize') == log.count('finalize'), log
print('GTK devices: hardware default, optional software, hotplug, trust and shutdown passed')
