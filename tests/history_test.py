# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = sys.argv[1]
with tempfile.TemporaryDirectory(prefix='crsign-history-') as directory:
    root = Path(directory)
    env = dict(os.environ, XDG_DATA_HOME=str(root/'data'))
    path = root/'data'/'crsign'/'history.json'
    def run(*args, ok=True):
        p = subprocess.run([binary, *map(str,args)], env=env, capture_output=True, timeout=10)
        assert (p.returncode == 0) == ok, (args,p.stdout,p.stderr)
        return p
    assert run('load').stdout.strip() == b'0'
    assert not path.parent.exists()
    assert run('view').stdout.strip()==b'grid'
    assert not path.parent.exists()
    assert run('hardware-only').stdout.strip()==b'true'
    assert not path.parent.exists()
    run('set-view','list')
    preferences=path.parent/'preferences.json'
    settings=json.loads(preferences.read_text())
    assert settings=={'version':1,'home':{'view':'list'}}
    assert run('view').stdout.strip()==b'list'
    settings['future']={'enabled':True};settings['home']['other']='retained'
    preferences.write_text(json.dumps(settings))
    run('set-view','grid')
    saved=json.loads(preferences.read_text())
    assert saved['future']==settings['future'] and saved['home']['other']=='retained'
    assert run('view').stdout.strip()==b'grid'
    run('set-hardware-only','false')
    assert run('hardware-only').stdout.strip()==b'false'
    run('set-view','list')
    settings=json.loads(preferences.read_text())
    assert settings['devices']['hardware_only'] is False
    assert settings['home']['view']=='list' and settings['future']==saved['future']
    settings['devices']['future']='retained'
    preferences.write_text(json.dumps(settings))
    run('set-hardware-only','true')
    assert run('hardware-only').stdout.strip()==b'true'
    assert json.loads(preferences.read_text())['devices']['future']=='retained'
    for invalid in ('{"version":1,"devices":[]}',
                    '{"version":1,"devices":{"hardware_only":"false"}}'):
        preferences.write_text(invalid)
        run('hardware-only',ok=False)
        run('set-hardware-only','false',ok=False)
        assert preferences.read_text()==invalid
    preferences.write_text('{"version":2}')
    run('set-view','list',ok=False)
    run('set-hardware-only','false',ok=False)
    assert preferences.read_text()=='{"version":2}'
    preferences.write_text(' '* (64*1024+1))
    run('view',ok=False)
    run('set-view','list',ok=False)
    preferences.unlink()

    output = root/'Firmado ñ.pdf'
    output.write_bytes(b'test')
    run('record', output)
    initial = json.loads(path.read_text())
    assert initial['version'] == 1 and len(initial['documents']) == 1
    assert initial['documents'][0]['path'] == str(output)
    run('favorite', output, 'true')
    run('record', output)
    history = json.loads(path.read_text())
    assert len(history['documents']) == 1 and history['documents'][0]['favorite']
    assert history['documents'][0]['signed_at'] > initial['documents'][0]['signed_at']
    # Concurrent processes merge additions rather than overwriting each other.
    processes = [subprocess.Popen([binary,'record',str(root/f'signed-{n}.pdf')],env=env,
                                 stdout=subprocess.PIPE,stderr=subprocess.PIPE) for n in range(40)]
    for p in processes:
        out,err=p.communicate(timeout=10)
        assert p.returncode == 0,(out,err)
    history = json.loads(path.read_text())
    assert len(history['documents']) == 41
    assert len({item['path'] for item in history['documents']}) == 41
    assert next(item for item in history['documents'] if item['path']==str(output))['favorite']
    run('favorite', root/'unknown.pdf','true',ok=False)
    before = path.read_bytes()
    for bad in (b'{',b'{"version":2,"documents":[]}',b'{"version":1,"documents":[{"path":"relative","favorite":false,"signed_at":1}]}',b' '* (16*1024*1024+1)):
        path.write_bytes(bad)
        run('load',ok=False)
        run('record',output,ok=False)
        assert path.read_bytes()==bad
    # A valid file near the limit must not be replaced by an oversized update.
    almost={'version':1,'documents':[{'path':str(root/'padding-'),'signed_at':1,'favorite':False}]}
    encode=lambda value: json.dumps(value,ensure_ascii=False,separators=(',',':')).encode()
    almost['documents'][0]['path']+='a'*(16*1024*1024-len(encode(almost))-32)
    bounded=encode(almost)
    path.write_bytes(bounded)
    run('record',output,ok=False)
    assert path.read_bytes()==bounded
    path.write_bytes(before)
    assert run('load').stdout.strip()==b'41'
    if os.name != 'nt':
        assert path.stat().st_mode & 0o077 == 0
print('History persistence, favorites, concurrency and size limits passed')
