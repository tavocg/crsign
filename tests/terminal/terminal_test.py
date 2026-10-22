# SPDX-License-Identifier: GPL-3.0-or-later
"""Real PTY tests: no echo, restored modes, cancellation and input bounds."""
import errno
import fcntl
import json
import os
from pathlib import Path
import select
import resource
import signal
import subprocess
import sys
import tempfile
import termios
import time


def interactive(command, data=b'1234\n', *, status=0, send_signal=None, env=None):
    master, slave = os.openpty()
    initial = termios.tcgetattr(slave)

    def controlling_terminal():
        os.setsid()
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        fcntl.ioctl(0, termios.TIOCSCTTY, 0)

    process = subprocess.Popen(command, stdin=slave, stdout=subprocess.PIPE,
                               stderr=slave, preexec_fn=controlling_terminal, env=env)
    transcript = b''
    try:
        deadline = time.monotonic() + 5
        while b'PIN: ' not in transcript:
            assert process.poll() is None, (command, process.returncode, transcript)
            assert time.monotonic() < deadline, (command, transcript)
            if select.select([master], [], [], 0.1)[0]:
                transcript += os.read(master, 4096)
        during = termios.tcgetattr(slave)
        assert not during[3] & (termios.ECHO | termios.ECHONL), during
        if data:
            os.write(master, data)
        if send_signal:
            process.send_signal(send_signal)
        stdout, _ = process.communicate(timeout=5)
        assert process.returncode == status, (command, process.returncode, stdout, transcript)
        assert termios.tcgetattr(slave) == initial, 'terminal settings were not restored'
        while select.select([master], [], [], 0)[0]:
            try:
                chunk = os.read(master, 4096)
            except OSError as error:
                if error.errno == errno.EIO:
                    break
                raise
            if not chunk:
                break
            transcript += chunk
        for secret in (b'1234', b'12\xc3\xb1', b'77777777'):
            assert secret not in transcript and secret not in stdout, (stdout, transcript)
        return stdout, transcript
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)


def standalone(driver):
    for arguments, data in [([], b'1234\n'), (['unicode'], b'12\xc3\xb1\n'),
                            (['empty'], b'\n'), ([], b'12X\x7f34\n'),
                            ([], b'12\xc3\xb1\x7f34\n'),
                            ([], b'wrong\x151234\n'),
                            (['limit'], b'7' * 1024 + b'\n')]:
        output, _ = interactive([driver, *arguments], data)
        assert output == b'PIN accepted; buffer cleared\n'
    output, transcript = interactive([driver], b'7' * 1025 + b'\n', status=1)
    assert not output and b'exceeds 1024' in transcript
    output, transcript = interactive([driver], b'\x04', status=1)
    assert not output and b'cancelled' in transcript
    interactive([driver], b'12\x03', status=-signal.SIGINT)
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGQUIT):
        interactive([driver], b'12', send_signal=sig, status=-sig)
    result = subprocess.run([driver], input=b'1234\n', capture_output=True, timeout=5)
    assert result.returncode == 1 and not result.stdout
    assert b'interactive terminal is required' in result.stderr
    assert b'PIN: ' not in result.stderr


def signing(sign):
    with tempfile.TemporaryDirectory(prefix='sign-pin-') as directory:
        root = Path(directory)
        source, target = root / 'input.txt', root / 'signed.txt'
        source.write_bytes(b'PIN signing fixture')
        env = os.environ.copy()
        env['SIGN_MOCK_DATA'] = str(source)
        env['SIGN_MOCK_LOGIN'] = '1'
        command = [sign, str(source), '-l', 'B-B', '-o', str(target), '--json']
        output, _ = interactive(command, env=env)
        assert json.loads(output)['ok']
        assert target.read_bytes() == source.read_bytes()
        target.unlink()
        env['SIGN_MOCK_SIGN_FAIL'] = '1'
        output, _ = interactive(command, status=1, env=env)
        assert not json.loads(output)['ok'] and not target.exists()
        del env['SIGN_MOCK_SIGN_FAIL']
        result = subprocess.run(command, input=b'1234\n', capture_output=True, env=env, timeout=5)
        assert result.returncode == 1 and not target.exists()
        assert b'interactive terminal is required' in result.stderr
        env['SIGN_MOCK_PROTECTED'] = '1'
        result = subprocess.run(command, input=b'', capture_output=True, env=env, timeout=5)
        assert result.returncode == 0 and b'PIN: ' not in result.stderr
        assert json.loads(result.stdout)['ok'] and target.read_bytes() == source.read_bytes()


if __name__ == '__main__':
    if len(sys.argv) == 3:
        signing(sys.argv[2])
    else:
        standalone(sys.argv[1])
    print('Interactive PIN checks passed')
