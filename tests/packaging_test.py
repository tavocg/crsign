# SPDX-License-Identifier: GPL-3.0-or-later
"""Check native dependency graphs and relocation rules without native toolchains."""
import hashlib
import importlib.util
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
source = Path(__file__).resolve().parents[1]
def module(name, file):
    spec = importlib.util.spec_from_file_location(name, source / file)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result
package = module('package_native', 'scripts/package-native.py')
bootstrap = module('bootstrap_native', 'scripts/bootstrap-native.py')
opensc = module('build_opensc', 'scripts/build-opensc.py')


def macho(*dependencies, rpath=None, identity=False):
    text = 'cmd LC_ID_DYLIB\n name @rpath/self.dylib (offset 24)\n' if identity else ''
    if rpath:
        text += f'cmd LC_RPATH\n path {rpath} (offset 12)\n'
    for dependency in dependencies:
        text += f'cmd LC_LOAD_DYLIB\n name {dependency} (offset 24)\n'
    return text


class Packaging(unittest.TestCase):
    def test_opensc_build_copy_normalizes_templates_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            original, copied = root / 'checkout', root / 'build-source'
            original.mkdir()
            (original / 'configure.ac').write_bytes(b'AC_CHECK_HEADERS([a \\\r\n b])\r\n')
            (original / 'fixture.der').write_bytes(b'\x00\r\n\xff')
            (original / '.git').write_text('gitdir: unused\n')
            opensc.prepare_source(original, copied)
            self.assertIn(b'\r\n', (original / 'configure.ac').read_bytes())
            self.assertNotIn(b'\r', (copied / 'configure.ac').read_bytes())
            self.assertEqual((copied / 'fixture.der').read_bytes(), b'\x00\r\n\xff')
            self.assertFalse((copied / '.git').exists())

    def test_source_archive_includes_vendor_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('libssig', 'opensc'):
                checkout = root / 'vendor' / name
                checkout.mkdir(parents=True)
                (checkout / '.git').write_text('gitdir: unused\n')
                (checkout / 'COPYING').write_text(name)
            (root / 'README.md').write_text('CRSign')
            target = root / 'sources.tar.gz'
            def tracked(command):
                return b'README.md\0' if Path(command[2]) == root else b'COPYING\0'
            with patch.object(package.subprocess, 'check_output', side_effect=tracked):
                package.sources_archive(root, target)
            with tarfile.open(target) as archive:
                self.assertEqual(set(archive.getnames()), {
                    'crsign/README.md', 'crsign/vendor/libssig/COPYING',
                    'crsign/vendor/opensc/COPYING'})

    def test_opensc_configure_uses_posix_paths_on_windows(self):
        path = Path('source')
        with patch.object(opensc.os, 'name', 'nt'), patch.object(
                opensc.subprocess, 'check_output', return_value='/d/source\n') as convert:
            self.assertEqual(opensc.configure_path(path), '/d/source')
            convert.assert_called_once_with(['cygpath', '-u', str(path)], text=True)

    def test_parsers(self):
        self.assertEqual(package.pe_dependencies(' DLL Name: foo.dll  \n DLL Name: USER32.dll\r\n'), ['foo.dll', 'USER32.dll'])
        text = macho('@rpath/lib with spaces.dylib', '/usr/lib/libSystem.B.dylib', rpath='@loader_path/../lib', identity=True)
        self.assertEqual(package.macho_commands(text), (['@rpath/lib with spaces.dylib', '/usr/lib/libSystem.B.dylib'], ['@loader_path/../lib'], True))

    def test_windows_closure_and_cycles(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / 'sdk'
            sdk.mkdir()
            for name in ('sign.exe', 'A.dll', 'b.dll'):
                (sdk / name).write_bytes(name.encode())
            graph = {'sign.exe': 'DLL Name: a.dll\nDLL Name: api-ms-win-crt-runtime-l1-1-0.dll\n',
                     'a.dll': 'DLL Name: b.dll\n', 'b.dll': 'DLL Name: A.dll\n'}
            bundle = package.Bundle(root / 'package', [sdk], platform='win32')
            bundle.copy(sdk / 'sign.exe', Path('bin/sign.exe'))
            with patch.object(package, 'command', side_effect=lambda *args: graph[Path(args[-1]).name.lower()]):
                bundle.close()
            self.assertEqual({p.name for p in (root / 'package/bin').iterdir()}, {'sign.exe', 'a.dll', 'b.dll'})

    def test_macos_rewrites_and_signs_copies(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / 'sdk with spaces'
            sdk.mkdir()
            for name in ('sign', 'a.dylib', 'b.dylib'):
                (sdk / name).write_bytes(name.encode())
            graph = {'sign': macho('@rpath/a.dylib', rpath=str(sdk)),
                     'a.dylib': macho(str(sdk / 'b.dylib'), '/usr/lib/libSystem.B.dylib', identity=True),
                     'b.dylib': macho(identity=True)}
            bundle = package.Bundle(root / 'package', [sdk], platform='darwin')
            bundle.copy(sdk / 'sign', Path('bin/sign'))
            with patch.object(package, 'command', side_effect=lambda *args: graph[Path(args[-1]).name]), patch.object(package.subprocess, 'run') as run:
                bundle.close()
            commands = [call.args[0] for call in run.call_args_list]
            self.assertTrue(any('@loader_path/../lib/a.dylib' in call for call in commands))
            self.assertTrue(any('@loader_path/b.dylib' in call for call in commands))
            self.assertTrue(all(str(root / 'package') in call[-1] for call in commands))
            self.assertEqual(sum(call[0] == 'codesign' for call in commands), 3)

    def test_missing_and_colliding_dependencies_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            a, b = root / 'a.dll', root / 'b.dll'
            a.write_bytes(b'a'); b.write_bytes(b'b')
            bundle = package.Bundle(root / 'package', [root], platform='win32')
            bundle.copy(a, Path('bin/same.dll'))
            with self.assertRaisesRegex(RuntimeError, 'collision'):
                bundle.copy(b, Path('bin/same.dll'))
            with self.assertRaisesRegex(RuntimeError, 'Unresolved'):
                bundle.find('missing.dll', a, [])

    def test_bootstrap_checks_cached_hash_before_extraction(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            version = bootstrap.SOURCES['tomlc17'][0]
            (cache / f'tomlc17-{version}.tar.gz').write_bytes(b'corrupt')
            with self.assertRaisesRegex(RuntimeError, 'Checksum mismatch'):
                bootstrap.fetch('tomlc17', cache, True)

    def test_bootstrap_rejects_path_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            archive = cache / 'test-1.tar.gz'
            with tarfile.open(archive, 'w:gz') as out:
                out.addfile(tarfile.TarInfo('root/../../outside'))
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            with patch.dict(bootstrap.SOURCES, test=('1', 'unused', digest)):
                with self.assertRaisesRegex(RuntimeError, 'Invalid archive path'):
                    bootstrap.fetch('test', cache, True)

if __name__ == '__main__':
    unittest.main()
