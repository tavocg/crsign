# SPDX-License-Identifier: GPL-3.0-or-later
"""Image decoding, aspect ratio, profiles and real signed PDF appearances."""
import binascii
import json
import os
import re
import xml.etree.ElementTree as ET
from datetime import datetime
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

sign, args_driver = sys.argv[1:]

def pdf(rotation=0):
    objects = [b'<< /Type /Catalog /Pages 2 0 R >>',
               b'<< /Type /Pages /Count 1 /Kids [3 0 R] >>',
               f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Rotate {rotation} /Resources << >> >>'.encode()]
    result = b'%PDF-1.7\n'
    offsets = [0]
    for i, obj in enumerate(objects, 1):
        offsets.append(len(result))
        result += f'{i} 0 obj\n'.encode() + obj + b'\nendobj\n'
    start = len(result)
    result += b'xref\n0 4\n0000000000 65535 f \n'
    result += b''.join(f'{n:010} 00000 n \n'.encode() for n in offsets[1:])
    return result + f'trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n'.encode()

def png(width, height):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', binascii.crc32(tag + data))
    # Four coloured quadrants, with transparency in the bottom right.
    rows = b''
    for y in range(height):
        rows += b'\0' + b''.join(
            bytes((255, 0, 0, 255) if y < height // 2 and x < width // 2 else
                  (0, 255, 0, 255) if y < height // 2 else
                  (0, 0, 255, 255) if x < width // 2 else (0, 0, 0, 128))
            for x in range(width))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')

with tempfile.TemporaryDirectory(prefix='crsign-appearance-') as directory:
    root = Path(directory)
    env = os.environ.copy()
    env['XDG_CONFIG_HOME'] = str(root / 'config')
    source = root / 'document.pdf'
    source.write_bytes(pdf())
    image = root / 'image.png'
    image.write_bytes(png(60, 30))
    svg = root / 'mark.svg'
    svg.write_text("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 50'><rect width='100' height='50' fill='red'/></svg>")
    output = root / 'signed.pdf'
    def run(*arguments, success=True, program=sign):
        output.unlink(missing_ok=True)
        result = subprocess.run([program, *map(str, arguments)], env=env, capture_output=True, timeout=15)
        assert (result.returncode == 0) == success, (arguments, result.stdout, result.stderr)
        if program == sign:
            assert output.exists() == success, (arguments, result.stderr)
            if success:
                assert output.read_bytes().startswith(source.read_bytes())
        return result
    geometry = ('-n', '1', '-x', '24', '-y', '36')
    common = (source, '-o', output, *geometry)
    # Default certificate text must escape XML metacharacters and UTF-8 correctly.
    env['SIGN_APPEARANCE_TEST_PREVIEW'] = str(root / 'default.png')
    run(*common)
    assert (root / 'default.png').stat().st_size > 1000
    env['SIGN_APPEARANCE_TEST_PREVIEW'] = str(root / 'default.svg')
    run(*common)
    text = [element.text for element in ET.parse(root / 'default.svg').iter() if element.tag.endswith('}text')]
    assert text[:2] == ['Ana & Compañía <CR>', 'Firmado digitalmente']
    assert text[3] == 'Representación gráfica. Verifique la validez.'
    assert re.fullmatch(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{2}:\d{2}', text[2]), text[2]
    assert datetime.fromisoformat(text[2]).tzinfo is not None
    del env['SIGN_APPEARANCE_TEST_PREVIEW']
    def rect_size():
        matches = re.findall(rb'/Rect \[([^\]]+)\]', output.read_bytes())
        coords = list(map(float, matches[-1].split()))
        return (coords[2] - coords[0], coords[3] - coords[1])
    for asset in (image, svg):
        for size in ((), ('-w', '160'), ('-h', '80')):
            run(*common, '-i', asset, *size)
            assert rect_size() == ((144, 72) if not size else (160, 80))
    # Produce a JPEG with the same decoder library, then load it again.
    jpeg = root / 'image.jpg'
    env['SIGN_APPEARANCE_TEST_PREVIEW'] = str(jpeg)
    run(*common, '-i', image)
    del env['SIGN_APPEARANCE_TEST_PREVIEW']
    run(*common, '-i', jpeg)
    # EXIF orientation 6 rotates the JPEG clockwise, changing its aspect ratio.
    exif = b'Exif\0\0II*\0\x08\0\0\0' + struct.pack('<H', 1) + struct.pack('<HHI', 0x112, 3, 1) + struct.pack('<H', 6) + b'\0\0' + b'\0' * 4
    rotated = root / 'oriented.jpg'
    rotated.write_bytes(jpeg.read_bytes()[:2] + b'\xff\xe1' + struct.pack('>H', len(exif) + 2) + exif + jpeg.read_bytes()[2:])
    run(*common, '-i', rotated)
    assert rect_size() == (144, 288)
    for rotation in (90, 180, 270):
        source.write_bytes(pdf(rotation))
        run(*common, '-i', image)
    source.write_bytes(pdf())
    for bad in (('-n', '2'), ('-x', '600'), ('-y', '790'), ('-w', '11'),
                ('-h', '721'), ('-w', '12'), ('-w', '720'), ('-x', 'nan'),
                ('-n', '0'), ('-n', '1.5'), ('-n', ' 1'), ('-x', '-1'),
                ('-w', '80', '-h', '40'), ('-i', root / 'absent')):
        run(*common, '-i', image, *bad, success=False)
    for incomplete in (('-n', '1'), ('-x', '0', '-y', '0'), ('-i', image), ('-h', '48')):
        run(source, '-o', output, *incomplete, success=False)
    broken = root / 'broken.png'
    broken.write_bytes(b'not an image')
    run(*common, '-i', broken, success=False)
    config = root / 'config' / 'crsign' / 'crsign.toml'
    config.parent.mkdir(parents=True)
    profile = 'version=0.1\n[[profiles]]\nlabel="mark"\nsignature_image="mark.svg"\n'
    config.write_text(profile)
    (config.parent / 'mark.svg').write_bytes(svg.read_bytes())
    def options(*a):
        return json.loads(run(source, '-p', 'mark', *a, program=args_driver).stdout)
    a = options()
    assert a['page_number'] == 0 and a['width'] == 0 and a['height'] == 0
    assert Path(a['signature_image']) == config.parent / 'mark.svg'
    # Selecting a profile image alone keeps signing invisible, without loading it.
    run(source, '-o', output, '-p', 'mark')
    assert b'/Subtype /Image' not in output.read_bytes()
    run(source, '-o', output, '-p', 'mark', *geometry)
    assert rect_size() == (144, 72)
    for size in (('-w', '160'), ('-h', '80')):
        run(source, '-o', output, '-p', 'mark', *geometry, *size)
        assert rect_size() == (160, 80)
    override = root / 'override.svg'
    override.write_text("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 90 30'><rect width='90' height='30' fill='blue'/></svg>")
    for arguments in (('-i', override, '-p', 'mark'), ('-p', 'mark', '-i', override)):
        run(source, '-o', output, *geometry, *arguments, '-w', '180')
        assert rect_size() == (180, 60)
    # A missing profile asset is only an error when visible signing is requested.
    (config.parent / 'mark.svg').unlink()
    run(source, '-o', output, '-p', 'mark')
    run(source, '-o', output, '-p', 'mark', *geometry, success=False)
    run(source, '-o', output, '-p', 'mark', *geometry, '-i', override)
    for incomplete in (('-n', '1'), ('-x', '24'), ('-h', '48')):
        run(source, '-o', output, '-p', 'mark', *incomplete, success=False)
    # Geometry belongs exclusively to CLI arguments.
    for field in ('page_number=1', 'x=24', 'y=36', 'width=144', 'height=48'):
        config.write_text(profile + field + '\n')
        result = run(source, '-p', 'mark', *geometry, program=args_driver, success=False)
        assert b'Unknown profile field' in result.stderr
    for invalid in ('signature_image=42',):
        config.write_text('version=0.1\n[[profiles]]\nlabel="mark"\n' + invalid + '\n')
        run(source, '-p', 'mark', *geometry, program=args_driver, success=False)
    config.write_text('version=0.1\n[[profiles]]\nlabel="mark"\nsignature_image=""\n')
    empty = run(source, '-p', 'mark', *geometry, program=args_driver)
    assert json.loads(empty.stdout)['signature_image'] is None
print('Visible PDF signature checks passed')
