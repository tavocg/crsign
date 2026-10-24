# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import subprocess
import sys
import tempfile

def pdf():
    objects=[b'<< /Type /Catalog /Pages 2 0 R >>', b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
             b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>']
    data=b'%PDF-1.7\n'; offsets=[]
    for n, obj in enumerate(objects,1):
        offsets.append(len(data));data+=f'{n} 0 obj\n'.encode()+obj+b'\nendobj\n'
    start=len(data)
    data+=b'xref\n0 4\n0000000000 65535 f \n'+b''.join(f'{n:010} 00000 n \n'.encode() for n in offsets)
    return data+f'trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n'.encode()
with tempfile.TemporaryDirectory(prefix='crsign-home-') as directory:
    root=Path(directory)
    data=pdf()
    for n in range(57): (root/f'Documento firmado número {n:02}.pdf').write_bytes(data)
    env=dict(os.environ,XDG_DATA_HOME=str(root/'data'),XDG_CONFIG_HOME=str(root/'config'))
    subprocess.run([sys.argv[1],str(root),sys.argv[2]],env=env,check=True,timeout=60)
print('Home pagination, favorites, thumbnails, list mode and fuzzy search passed')
