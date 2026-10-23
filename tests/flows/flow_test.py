# SPDX-License-Identifier: GPL-3.0-or-later
"""Flow validation, canonical serialization and sequential execution without a card."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, schema_path = sys.argv[1:]
# Native CI does not need the external JSON Schema dependency.
try:
    import jsonschema
except ImportError:
    jsonschema = None
schema = json.loads(Path(schema_path).read_text())
with tempfile.TemporaryDirectory(prefix='crsign-flow-') as directory:
    root = Path(directory)
    request = root / 'flow.json'
    signature = root / 'fixture.bin'
    signature.write_bytes(b'signature fixture\0\xff')
    source = root / 'archivo ñ.bin'
    source.write_bytes(bytes(range(256)))
    env = os.environ.copy()
    env['SIGN_MOCK_DATA'] = str(signature)
    env['XDG_CONFIG_HOME'] = str(root / 'config')
    config = root / 'config' / 'crsign' / 'crsign.toml'
    config.parent.mkdir(parents=True)
    config.write_text('intentionally invalid configuration; flows must never read this')

    def call(value, mode='parse', success=True, login=False, protected=False):
        request.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
        local = env.copy()
        if login:
            local['SIGN_MOCK_LOGIN'] = '1'
        if protected:
            local['SIGN_MOCK_PROTECTED'] = '1'
        p = subprocess.run([binary, mode, str(request), str(root)], env=local,
                           capture_output=True, timeout=15)
        assert (p.returncode == 0) == success, (value, mode, p.stdout, p.stderr)
        if success or p.stdout:
            result = json.loads(p.stdout)
            if mode == 'parse' and jsonschema:
                jsonschema.validate(result, schema)
            return result, p.stderr
        return None, p.stderr

    def flow(*documents):
        return {'version': 1, 'documents': list(documents)}

    basic = {'input': source.name, 'level': 'B-B'}
    canonical, _ = call(flow(basic))
    assert canonical['documents'][0]['input'] == str(source)
    assert canonical['documents'][0]['output'] == str(root / 'archivo ñ-signed.bin')
    assert call(canonical)[0] == canonical
    rich = dict(basic, output='signed.p7m', token='pkcs11:token=Mock', cert_id='00ff',
                algorithm='rsa-pss', hash='sha384', format='cades', mime_type='application/octet-stream',
                detached=True, modules=['one.so', 'two.so'], ca='root.pem', intermediates='chain.pem',
                tsa={'url':'https://tsa.example.test/', 'ca':'tsa.pem', 'intermediates':'tsa-chain.pem'},
                no_bundled_certs=True, appearance={'enabled':False, 'image':'unavailable.svg', 'x':0})
    rich_json, _ = call(flow(rich))
    options = rich_json['documents'][0]
    for key in ('input','output','ca','intermediates'):
        assert Path(options[key]).is_absolute()
    assert options['modules'] == [str(root/'one.so'), str(root/'two.so')]
    assert options['tsa']['ca'] == str(root/'tsa.pem')
    assert options['appearance']['image'] == str(root/'unavailable.svg')
    for document in ({}, {'input':None}, dict(basic, hash='SHA256'), dict(basic, algorithm='rsa'),
                     dict(basic, format='cms'), dict(basic, format='pades', detached=True),
                     dict(basic, cert_id='f'), dict(basic, cert_id='zz'), dict(basic, modules='one'),
                     dict(basic, modules=['']), dict(basic, cert_bundles=['missing']),
                     dict(basic, detached=1), dict(basic, tsa={'pin':'DO-NOT-LOG'}),
                     dict(basic, pin='DO-NOT-LOG'), dict(basic, pincmd=['password-helper']),
                     dict(basic, profile='bccr'), dict(basic, json=True)):
        _, error = call(flow(document), success=False)
        assert b'DO-NOT-LOG' not in error
    for token in ('bad', 'pkcs11:token=Mock?pin-value=DO-NOT-LOG',
                  'pkcs11:token=Mock?pin-source=file:/DO-NOT-LOG',
                  'pkcs11:token=Mock?%70in-value=DO-NOT-LOG'):
        _, error = call(flow(dict(basic, token=token)), success=False)
        assert b'DO-NOT-LOG' not in error
    for appearance in ({}, {'enabled':'false'}, {'enabled':True},
                        {'enabled':True,'page_number':1,'x':0},
                        {'enabled':False,'width':20,'height':20},
                        {'enabled':False,'width':11}, {'enabled':False,'height':721},
                        {'enabled':False,'x':-1}, {'enabled':False,'page_number':True},
                        {'enabled':False,'page_number':1.5}, {'enabled':False,'pin':'DO-NOT-LOG'}):
        call(flow(dict(basic, appearance=appearance)), success=False)
    visible = {'enabled':True,'page_number':1,'x':0,'y':0}
    call(flow(dict(basic, appearance=visible)))
    call(flow(dict(basic, format='cades', appearance=visible)), success=False)
    call(flow(dict(basic, detached=True, appearance=visible)), success=False)
    call(flow(dict(basic, appearance=visible)), mode='run', success=False)
    assert not (root / 'archivo ñ-signed.bin').exists()
    for invalid in ({}, {'version':2,'documents':[basic]}, {'version':True,'documents':[basic]},
                    {'version':'1','documents':[basic]}, flow(), flow(basic,{}),
                    dict(flow(basic), parallel=True), [], flow(*([basic]*1025)),
                    b'{"version":1,"version":1,"documents":[]}',
                    b'{"version":1,"documents":[{"input":"x","input":"y"}]}',
                    b'{"version":1,"documents":[]} trailing', b'{}\0anything', b'\xff',
                    b'{"version":1,"documents":[{"input":"\\u0000"}]}', b'['*2100, b' '* (8*1024*1024 + 1)):
        call(invalid, success=False)
    # Parse the whole flow before any side effect, including path collisions.
    call(flow(basic, dict(basic, output='archivo ñ-signed.bin')), success=False)
    call(flow(dict(basic, output=source.name)), success=False)
    call(flow(dict(basic, output='second.bin'), {'input':'second.bin'}), success=False)
    assert not (root/'archivo ñ-signed.bin').exists()
    # Arbitrary binary input and all non-PDF output formats use the same engine.
    for format in ('cades','xades','jades'):
        out = root / ('result-' + format)
        result, _ = call(flow(dict(basic, format=format, output=out.name)), mode='run')
        assert result['completed'] == 1 and result['failed_document'] == -1
        assert out.read_bytes() == signature.read_bytes()
    # A disabled appearance never loads its file or requires PDF input.
    result, _ = call(flow(dict(basic, output='disabled.p7m', appearance={'enabled':False,'image':'missing.svg'})), mode='run')
    assert result['completed'] == 1
    # The second file fails: preserve the first output and never start the third.
    outputs = [root/f'ordered-{i}.p7m' for i in range(3)]
    request_flow = flow(dict(basic, output=outputs[0].name),
                        {'input':'missing.bin','output':outputs[1].name,'level':'B-B'},
                        dict(basic, output=outputs[2].name))
    result, _ = call(request_flow, mode='run', success=False)
    assert result['completed'] == 1 and result['failed_document'] == 1
    assert outputs[0].read_bytes() == signature.read_bytes()
    assert not outputs[1].exists() and not outputs[2].exists()
    # Existing output is preserved and stops the flow.
    result, _ = call(request_flow, mode='run', success=False)
    assert result['completed'] == 0 and result['failed_document'] == 0
    assert outputs[0].read_bytes() == signature.read_bytes()
    for mode in ('run','reentrant','concurrent'):
        result, _ = call(flow(dict(basic, output=f'{mode}.p7m')), mode=mode, login=True)
        assert result['completed'] == 1 and result['pin_requests'] == 1
    result, _ = call(flow(dict(basic, output='no-pin.p7m')), mode='no-pin', login=True, success=False)
    assert result['completed'] == 0 and result['pin_requests'] == 0
    assert not (root/'no-pin.p7m').exists()
    result, _ = call(flow(dict(basic, output='protected.p7m')), mode='no-pin', login=True, protected=True)
    assert result['completed'] == 1 and result['pin_requests'] == 0
print('Flow parsing and fail-fast execution checks passed')
