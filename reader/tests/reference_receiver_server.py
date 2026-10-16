#!/usr/bin/env python3
"""Test-owned TLS wrapper around the unchanged frozen companion App/Server.

No provider credentials, network deployment, or reader implementation belongs here.
"""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import signal
import ssl
import sys
import threading
import uuid

EMAIL = 'receiver-cross-test@example.invalid'
PASSWORD = 'synthetic-local-receiver-test-password'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--root', required=True, type=Path)
    args = p.parse_args()
    os.umask(0o077)
    root = args.root.resolve()
    source = root / 'source'
    pins = json.loads((source / 'FROZEN-SOURCE.json').read_text())
    for name, expected in pins['files'].items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError('Frozen reference source changed')
    for package, expected in [('pypdf', '6.18.1'), ('Pillow', '12.3.0')]:
        if importlib.metadata.version(package) != expected:
            raise RuntimeError('Reference dependency version differs from pin')
    sys.path.insert(0, str(source))
    from companion.server import App, Server
    from companion.db import password_hash
    app = App(root / 'data', 'https://127.0.0.1:1', dev=False, api_key='')
    if not app.db.one('SELECT id FROM users WHERE email=?', (EMAIL,)):
        app.db.run('INSERT INTO users VALUES(?,?,?)', (str(uuid.uuid4()), EMAIL, password_hash(PASSWORD)))
    server = Server(('127.0.0.1', 0), app)
    port = server.server_address[1]
    app.origin = f'https://127.0.0.1:{port}'
    app.host = f'127.0.0.1:{port}'
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(str(root / 'tls/server.pem'), str(root / 'tls/server.key'))
    server.socket = context.wrap_socket(server.socket, server_side=True)
    public = {'origin': app.origin, 'port': port, 'ca_path': str(root / 'tls/ca.pem'),
              'root': str(root), 'pid': os.getpid(), 'start_ticks': Path('/proc/self/stat').read_text().split()[21],
              'reference_revision': pins['revision'], 'dev': False, 'provider_configured': False}
    (root / 'public-info.json').write_text(json.dumps(public, indent=2) + '\n')
    signal.signal(signal.SIGTERM, lambda *_: threading.Thread(target=server.shutdown, daemon=True).start())
    print(json.dumps({'ready': True, 'origin': app.origin, 'ca_path': public['ca_path']}), flush=True)
    try:
        server.serve_forever()
    finally:
        server.server_close()
        app.close()
        (root / 'stopped.json').write_text(json.dumps({'pid': os.getpid(), 'stopped': True}) + '\n')


if __name__ == '__main__':
    main()
