# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

driver = Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='crsign-runtime-') as temporary:
    root = Path(temporary) / 'instalación ñ 文件'
    (root / 'bin').mkdir(parents=True)
    executable = root / 'bin' / driver.name
    shutil.copy2(driver, executable)
    env = os.environ.copy()
    for name in ('GSETTINGS_SCHEMA_DIR', 'FONTCONFIG_FILE', 'GIO_EXTRA_MODULES', 'GDK_PIXBUF_MODULE_FILE'):
        env.pop(name, None)
    env['XDG_DATA_DIRS'] = 'existing-data'
    env['XDG_CACHE_HOME'] = str(root / 'cache')
    def run(success=True):
        result = subprocess.run([str(executable)], env=env, encoding='utf-8', capture_output=True, timeout=10)
        assert (result.returncode == 0) == success, result.stderr
        return dict(line.split('=', 1) for line in result.stdout.splitlines())
    # Normal builds do not modify resource lookup or execute the helper.
    assert run()['XDG_DATA_DIRS'] == 'existing-data'
    (root / 'share/crsign').mkdir(parents=True)
    (root / 'share/crsign/portable').write_text('1\n')
    for folder in ('share/glib-2.0/schemas', 'lib/gio/modules', 'lib/gdk-pixbuf-loaders', 'etc/fonts'):
        (root / folder).mkdir(parents=True)
    (root / 'etc/fonts/fonts.conf').write_text('<fontconfig/>')
    template = root / 'share/crsign/loaders.cache.in'
    template.write_text('"@CRSIGN_PREFIX@/lib/gdk-pixbuf-loaders/loader.so"\n')
    options = run()
    assert options['GSETTINGS_SCHEMA_DIR'] == str(root / 'share/glib-2.0/schemas')
    assert options['FONTCONFIG_FILE'] == str(root / 'etc/fonts/fonts.conf')
    assert options['GIO_EXTRA_MODULES'] == str(root / 'lib/gio/modules')
    assert options['XDG_DATA_DIRS'] == str(root / 'share') + os.pathsep + 'existing-data'
    assert Path(options['GDK_PIXBUF_MODULE_FILE']).is_file()
    cache = Path(options['GDK_PIXBUF_MODULE_FILE']).read_text()
    assert '@CRSIGN_PREFIX@' not in cache and 'loader.so' in cache
    # An explicit user cache/lookup path is retained even when the bundled template is unreadable.
    env['GDK_PIXBUF_MODULE_FILE'] = 'user-cache'
    env['GSETTINGS_SCHEMA_DIR'] = 'user-schemas'
    template.unlink()
    template.mkdir()
    assert run()['GSETTINGS_SCHEMA_DIR'] == 'user-schemas'
    assert run()['GDK_PIXBUF_MODULE_FILE'] == 'user-cache'
    del env['GDK_PIXBUF_MODULE_FILE']
    run(success=False)
print('Relocated resources, Unicode paths, cache and overrides passed')
