# SPDX-License-Identifier: GPL-3.0-or-later
import os
import subprocess
import sys

driver = sys.argv[1]
env = os.environ.copy()
for name in ('XDG_DATA_DIRS', 'GSETTINGS_SCHEMA_DIR', 'GSETTINGS_SCHEMAS_PATH'):
    env.pop(name, None)
result = subprocess.run([driver], env=env, capture_output=True, text=True, timeout=10)
assert result.returncode == 0, result.stderr
print('GTK settings schema discovery passed')
