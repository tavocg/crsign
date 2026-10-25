# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the actual executable's direct imports, not just CMake's selections."""
from pathlib import Path
import re
import subprocess
import sys

binary = sys.argv[1]
if sys.platform == 'darwin':
    output = subprocess.check_output(['otool', '-L', binary], text=True)
    imports = [line.strip().split()[0] for line in output.splitlines()[1:]]
elif sys.platform == 'win32':
    output = subprocess.check_output(['objdump', '-p', binary], text=True)
    imports = re.findall(r'DLL Name:\s*(\S+)', output)
else:
    output = subprocess.check_output(['readelf', '-d', binary], text=True)
    imports = re.findall(r'\(NEEDED\).*?\[(.*?)\]', output)

assert imports, f'No dynamic imports found in {binary}: {output}'
# libssig and the embedded OpenSC provider must have no dynamic imports.
for dependency in imports:
    name = Path(dependency).name.lower()
    assert not re.match(r'(lib)?(ssig|opensc|crsign-opensc)([-.\d]|$)', name), dependency
print('Static libssig and OpenSC verified; other direct imports:', ', '.join(imports))
