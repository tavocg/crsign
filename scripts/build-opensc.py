#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a local OpenSC source tree as one static PKCS#11 provider archive.

No downloads. Used by both Nix and the explicit native bootstrap.
Requires Autoconf, Automake, Libtool, Make, pkg-config and OpenSSL headers.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def run(*args, cwd, env=None):
    subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)


def configure_path(path):
    # Autoconf treats ':' as a search-path separator; drive-letter source
    # paths therefore lose their drive when searching for auxiliary files.
    if os.name == 'nt':
        return subprocess.check_output(['cygpath', '-u', str(path)], text=True).strip()
    return path.as_posix()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, required=True)
    args = parser.parse_args()
    source, build, prefix = (p.resolve() for p in (args.source, args.build, args.prefix))
    build.mkdir(parents=True, exist_ok=True)
    run('sh', '-c', 'autoreconf -fi', cwd=source)
    env = dict(os.environ)
    # Namespace the public entry point and hide provider symbols from plugins.
    env['CPPFLAGS'] = env.get('CPPFLAGS', '') + ' -DC_GetFunctionList=crsign_opensc_get_function_list'
    env['CFLAGS'] = env.get('CFLAGS', '-O2') + ' -fvisibility=hidden'
    host = []
    if env.get('PCSC_PROVIDER'):
        host.append('--with-pcsc-provider=' + env['PCSC_PROVIDER'])
    if os.name == 'nt':
        target = subprocess.check_output(['gcc', '-dumpmachine'], text=True).strip()
        host = ['--host=' + target]
    run('sh', configure_path(source / 'configure'), *host, '--disable-shared', '--enable-static',
        '--disable-tests', '--disable-cmocka', '--disable-man', '--disable-doc',
        '--disable-notify', '--disable-readline', '--disable-openpace',
        '--disable-zlib', '--disable-minidriver', '--disable-strict',
        '--prefix=' + configure_path(prefix), cwd=build, env=env)
    for directory in ('common', 'scconf', 'ui', 'pkcs15init', 'sm', 'libopensc', 'pkcs11'):
        run('make', '-j4', '-C', build / 'src' / directory, cwd=build)
    # Merge object files, not archive files: this preserves normal static linker
    # reachability without --whole-archive or platform-specific linker flags.
    output = prefix / 'lib/libcrsign-opensc.a'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    ar = env.get('AR', 'ar')
    objects = build / 'provider-objects'
    if objects.exists():
        shutil.rmtree(objects)
    objects.mkdir()
    for index, archive in enumerate(sorted((build / 'src').glob('*/.libs/*.a'))):
        target = objects / str(index)
        target.mkdir()
        run(ar, 'x', archive, cwd=target)
        members = sorted(target.glob('*.o'))
        if members:
            run(ar, 'q', output, *members, cwd=target)
    run(env.get('RANLIB', 'ranlib'), output, cwd=build)
    notices = prefix / 'share/crsign-deps'
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / 'COPYING', notices / 'opensc-COPYING')


if __name__ == '__main__':
    main()
