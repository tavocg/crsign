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
    files.append(root/'third file.bin')
    files[-1].write_bytes(original)
    # Exercise the actual Home widgets against a separate receiving process.
    cases = [([], 1, {1}), ([(0, None)], 1, {1}),
             ([(0, None), (1, 'ctrl')], 0, {0, 1}),
             ([(0, None), (2, 'shift')], 1, {0, 1, 2}),
             ([(0, None), (1, 'ctrl')], 2, {2}),
             ([(0, None), (1, 'ctrl'), (0, None)], 0, {0})]
    for mode in ('home-grid', 'home-list'):
        for case, (clicks, dragged, expected) in enumerate(cases):
            run = root/f'{mode}-{case}'
            run.mkdir()
            env['XDG_DATA_HOME'] = str(run/'data')
            received, finished = run/'received', run/'finished'
            processes = []
            try:
                processes.append(subprocess.Popen([binary, 'file-target', str(received)], env=env))
                processes.append(subprocess.Popen([binary, mode, str(finished), '1', *map(str, files)], env=env))
                target = window('CRSign Drop Target')
                source = window('CRSign Drop Source')
                mouse('windowmove', source, 0, 0)
                mouse('windowmove', target, 700, 0)
                positions = Path(str(finished)+'.positions')
                deadline = time.monotonic()+5
                while not positions.exists() and time.monotonic()<deadline:
                    time.sleep(.05)
                points = [tuple(map(int, line.split())) for line in positions.read_text().splitlines()]
                assert len(points) == 3, points
                for index, modifier in clicks:
                    if modifier: mouse('keydown', modifier)
                    mouse('mousemove', '--window', source, *points[index])
                    mouse('click', 1)
                    if modifier: mouse('keyup', modifier)
                    time.sleep(.15)
                x, y = points[dragged]
                mouse('mousemove', '--window', source, x, y)
                mouse('mousedown', 1)
                time.sleep(.15)
                for step in range(1, 31):
                    mouse('mousemove', int(x+(850-x)*step/30), int(y+(180-y)*step/30))
                    time.sleep(.015)
                time.sleep(.15)
                mouse('mouseup', 1)
                for process in processes: assert process.wait(timeout=10) == 0
                assert set(received.read_text().splitlines()) == {str(files[i]) for i in expected}, (mode, case, received.read_text())
                assert finished.read_text() == 'keep'
                assert all(file.read_bytes() == original for file in files)
            finally:
                for process in processes:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
print('Native COPY/MOVE URI-list drops received; original files preserved')
