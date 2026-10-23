# SPDX-License-Identifier: GPL-3.0-or-later
"""A relocated executable must bind its installed catalog, not the build tree."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

executable, catalogs, locale_dir = map(Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='crsign-i18n-') as directory:
    prefix = Path(directory) / 'relocated prefix'
    destination = prefix / 'bin' / executable.name
    destination.parent.mkdir(parents=True)
    shutil.copy2(executable, destination)
    installed = prefix / locale_dir
    target = installed / 'es' / 'LC_MESSAGES' / 'crsign.mo'
    target.parent.mkdir(parents=True)
    shutil.copy2(catalogs / 'es' / 'LC_MESSAGES' / 'crsign.mo', target)
    subprocess.run([str(destination), 'es', str(installed)], check=True, timeout=10)
