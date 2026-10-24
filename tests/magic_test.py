# SPDX-License-Identifier: GPL-3.0-or-later
"""The bundled MIME database ignores user files; MAGIC stays an explicit override."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='crsign-magic-') as directory:
    home = Path(directory)
    rules = '0 string %PDF- Test PDF\n!:mime application/x-crsign-shadow\n'
    # Cover libmagic's Unix and Windows default user database locations.
    (home / '.magic.mgc').write_text(rules)
    (home / '.magic').mkdir()
    shadow = home / '.magic/magic.mgc'
    shadow.write_text(rules)
    env = os.environ.copy()
    env['HOME'] = env['USERPROFILE'] = env['LOCALAPPDATA'] = str(home)
    env.pop('MAGIC', None)

    def detect(expected, success=True):
        result = subprocess.run([sys.argv[1]], env=env, capture_output=True,
                                text=True, timeout=10)
        assert (result.returncode == 0) == success, (result.stdout, result.stderr)
        assert result.stdout.strip() == expected, (result.stdout, result.stderr)

    detect('application/pdf')
    env['MAGIC'] = str(shadow)
    detect('application/x-crsign-shadow')
    env['MAGIC'] = str(home / 'missing-database')
    detect('', success=False)
print('Embedded MIME database and explicit MAGIC override verified')
