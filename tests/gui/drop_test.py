# SPDX-License-Identifier: GPL-3.0-or-later
"""Transfer URI lists between separate GTK processes using real pointer events."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

binary, xdotool = sys.argv[1:]


def mouse(*args):
    return subprocess.run([xdotool, *map(str, args)], check=True,
                          capture_output=True, text=True).stdout.strip()


def window(title):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        found = subprocess.run([xdotool, 'search', '--onlyvisible', '--name',
                                '^' + title + '$'], capture_output=True, text=True)
        if found.returncode == 0:
            return found.stdout.splitlines()[0]
        time.sleep(0.05)
    raise AssertionError('Window did not appear: ' + title)


with tempfile.TemporaryDirectory(prefix='crsign-native-drop-') as directory:
    root = Path(directory)
    env = os.environ.copy()
    env['XDG_CONFIG_HOME'] = str(root/'config')
    files = [root/'first ñ & record.bin', root/'second.bin']
    original = b'Unsigned input: testing transfer rather than signature success.'
    for file in files:
        file.write_bytes(original)
    # GdkDragAction: COPY and COPY|MOVE. Negotiate COPY and retain originals.
    for mode, actions in (("source", 1), ("source", 3), ("file-source", 1)):
        count = "1" if mode == "file-source" else "2"
        received = root/f'received-{mode}-{actions}'
        finished = root/f'finished-{mode}-{actions}'
        processes = []
        try:
            processes.append(subprocess.Popen([binary, 'target', str(received), count], env=env))
            processes.append(subprocess.Popen([binary, mode, str(finished),
                                                str(actions), *map(str, files)], env=env))
            target = window('CRSign Drop Target')
            source = window('CRSign Drop Source')
            mouse('windowmove', source, 0, 0)
            mouse('windowmove', target, 500, 0)
            time.sleep(0.15)
            mouse('mousemove', 150, 150)
            mouse('mousedown', 1)
            for n in range(1, 31):
                mouse('mousemove', 150 + n*20, 150)
                time.sleep(0.015)
            time.sleep(0.15)
            mouse('mouseup', 1)
            for process in processes:
                assert process.wait(timeout=15) == 0
            assert received.read_text() == count
            assert finished.read_text() == 'keep', (actions, finished.read_text())
            assert all(file.read_bytes() == original for file in files)
        finally:
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
print('Native COPY/MOVE URI-list drops received; original files preserved')
