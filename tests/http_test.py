# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual curl transport against a loopback HTTP server."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import subprocess
import sys
import threading

seen = []
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self): self.handle_request()
    def do_POST(self): self.handle_request()
    def handle_request(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        seen.append((self.command, self.path, dict(self.headers), body))
        if self.path in ('/redirect', '/loop'):
            self.send_response(301)
            self.send_header('Location', '/reply' if self.path == '/redirect' else '/loop')
            self.end_headers()
        elif self.path == '/reject':
            self.send_response(406)
            self.end_headers()
        else:
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'reply')

server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    url = 'http://127.0.0.1:' + str(server.server_port)
    accepts = ['application/timestamp-reply', 'application/ocsp-response',
               'application/pkix-cert', 'application/pkix-crl']
    labels = ['TSA', 'OCSP', 'certificate', 'CRL']
    for kind in range(4):
        seen.clear()
        result = subprocess.run([sys.argv[1], str(kind), url + '/redirect'], capture_output=True)
        assert result.returncode == 0 and result.stdout == b'reply', result.stderr
        assert len(seen) == 2
        for method, path, headers, body in seen:
            assert method == ('POST' if kind < 2 else 'GET')
            assert body == (b'query' if kind < 2 else b'')
            assert accepts[kind] in headers['Accept']
            assert headers['User-Agent'].startswith('Sign/')
            if kind >= 2: assert 'Content-Type' not in headers
        result = subprocess.run([sys.argv[1], str(kind), url + '/reject'], capture_output=True)
        assert result.returncode == 1
        assert (labels[kind] + ' HTTP failure').encode() in result.stderr
        assert b'HTTP 406' in result.stderr and b'No error' not in result.stderr
    seen.clear()
    result = subprocess.run([sys.argv[1], '0', url + '/loop'], capture_output=True)
    assert result.returncode == 1 and len(seen) == 4
finally:
    server.shutdown()
    server.server_close()
    thread.join()
