# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise real signatures through GTK drag/drop and the asynchronous queue."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, signer, snapshots = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='crsign-gui-validation-') as directory:
    root = Path(directory)
    env = os.environ.copy()
    env['XDG_CONFIG_HOME'] = str(root/'config')
    env['SIGN_FLOW_TEST_ANCHOR'] = str(root/'anchor.der')
    (root/'payload.bin').write_bytes(bytes(range(256)) + b'\0signed arbitrary data\xff')
    (root/'wrong.bin').write_bytes(b'wrong original content')
    flow = root/'sign.json'
    flow.write_text(json.dumps(dict(version=1, documents=[
        dict(input='payload.bin', output='first.p7m', level='B-B'),
        dict(input='first.p7m', output='signed ñ & record.p7m', level='B-B'),
        dict(input='payload.bin', output='detached.p7s', level='B-B', detached=True)])))
    subprocess.run([signer, 'run', str(flow), str(root)], env=env, check=True,
                   capture_output=True, timeout=15)
    # A different process makes a distinct, untrusted certificate.
    env.pop('SIGN_FLOW_TEST_ANCHOR')
    flow.write_text(json.dumps(dict(version=1, documents=[
        dict(input='payload.bin', output='untrusted.p7m', level='B-B')])))
    subprocess.run([signer, 'run', str(flow), str(root)], env=env, check=True,
                   capture_output=True, timeout=15)
    config = root/'config'/'crsign'/'crsign.toml'
    config.parent.mkdir(parents=True)
    config.write_text('version=0.1\nca=' + json.dumps(str(root/'anchor.der')) + '\n'
                      'hash="sha512"\nformat="pades"\n'
                      'modules=["missing-module.so"]\npincmd=["missing-pin-command"]\n')
    subprocess.run([binary, str(root), str(Path(snapshots).absolute())], env=env,
                   check=True, timeout=25)
print('GTK validation queue, drag/drop, trust and detached signatures passed')
