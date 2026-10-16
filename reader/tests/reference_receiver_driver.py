#!/usr/bin/env python3
"""Synthetic browser-side protocol driver, never a substitute device receiver.

Device pairing approval, polling, download, ACK and tombstones must be driven by
production C++ Receiver + VerifiedHttpTransport. This driver holds browser cookies
in a private file and prints no authentication/device tokens.
"""
import argparse
import hashlib
import http.client
import importlib.util
import json
import os
from pathlib import Path
import signal
import ssl
import sys
import time
import uuid
from urllib.parse import urlsplit

EMAIL = 'receiver-cross-test@example.invalid'
PASSWORD = 'synthetic-local-receiver-test-password'


class Browser:
    def __init__(self, root):
        self.root = root
        self.info = json.loads((root / 'public-info.json').read_text())
        self.origin = self.info['origin']
        parsed = urlsplit(self.origin)
        if parsed.scheme != 'https' or parsed.hostname != '127.0.0.1':
            raise RuntimeError('Only the local test TLS origin is allowed')
        self.port = parsed.port
        self.context = ssl.create_default_context(cafile=self.info['ca_path'])
        self.path = root / 'browser-private.json'
        self.state = json.loads(self.path.read_text()) if self.path.exists() else {}

    def call(self, method, path, data=None, raw=None, headers=None, expected=(200,)):
        h = {'Host': f'127.0.0.1:{self.port}'}
        if self.state.get('cookie'):
            h['Cookie'] = self.state['cookie']
        if method != 'GET':
            h['Origin'] = self.origin
            h['X-CSRF-Token'] = self.state.get('csrf', '')
        if data is not None:
            h['Content-Type'] = 'application/json'
            raw = json.dumps(data).encode()
        h.update(headers or {})
        c = http.client.HTTPSConnection('127.0.0.1', self.port, context=self.context, timeout=40)
        try:
            c.request(method, path, raw, h)
            response = c.getresponse()
            body = response.read(2 * 1024 * 1024 + 1)
            if len(body) > 2 * 1024 * 1024:
                raise RuntimeError('Browser response exceeds bound')
            status = response.status
            cookie = response.getheader('Set-Cookie')
        finally:
            c.close()
        value = json.loads(body)
        if status not in expected:
            # Do not echo potentially sensitive response bodies.
            raise RuntimeError(f'Browser endpoint rejected request: HTTP {status}, expected {expected}')
        if cookie:
            self.state['cookie'] = cookie.split(';', 1)[0]
        if isinstance(value, dict) and 'csrf' in value:
            self.state['csrf'] = value['csrf']
        if self.state:
            temporary = self.path.with_suffix('.pending')
            temporary.write_text(json.dumps(self.state))
            temporary.chmod(0o600)
            temporary.replace(self.path)
        return value

    def login(self):
        self.call('POST', '/v1/auth/login', {'email': EMAIL, 'password': PASSWORD})
        value = self.call('GET', '/v1/me')
        if value['tom_configured']:
            raise RuntimeError('Unexpected provider configuration in synthetic test')

    def fixtures(self):
        source = self.root / 'source'
        sys.path.insert(0, str(source))
        spec = importlib.util.spec_from_file_location('reference_fixtures', source / 'tests/fixtures.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        directory = self.root / 'synthetic-fixtures'
        directory.mkdir(exist_ok=True, mode=0o700)
        functions = {'pdf': module.pdf, 'epub': module.epub, 'docx': module.docx,
                     'txt': lambda: b'Synthetic reader receiver text fixture.\nNo private content.\n',
                     'md': lambda: b'# Synthetic receiver Markdown\n\nA **bounded** local integration fixture.\n'}
        result = {}
        for extension, make in functions.items():
            target = directory / ('receiver.' + extension)
            if not target.exists():
                target.write_bytes(make())
            result[extension] = target
        return result

    def upload(self, device, path, new_import=False, expected_failure=None):
        data = path.read_bytes()
        result = self.call('POST', '/v1/jobs', {'device_id': device, 'filename': path.name,
                           'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'new_import': new_import}, expected=(expected_failure,) if expected_failure else (200, 201))
        if expected_failure:
            return {'rejected': True, 'expected_http_status': expected_failure, 'code': result.get('code', '')}
        if result['status'] == 'uploading':
            ident = result['job_id']
            for start in range(result['received'], len(data), 262144):
                block = data[start:start + 262144]
                self.call('PUT', f'/v1/jobs/{ident}/chunk', raw=block,
                          headers={'Content-Type': 'application/octet-stream', 'Content-Range': f'bytes {start}-{start + len(block) - 1}/{len(data)}'})
            self.call('POST', f'/v1/jobs/{ident}/complete', {})
        end = time.monotonic() + 40
        while time.monotonic() < end:
            state = self.call('GET', '/v1/jobs/' + result['job_id'])
            if state['status'] in ('waiting for device', 'failed', 'imported', 'deleted'):
                if state['status'] == 'failed':
                    raise RuntimeError('Frozen reference conversion failed for synthetic fixture: ' + path.suffix)
                return {key: state[key] for key in ('job_id', 'document_id', 'status', 'source_format', 'format', 'converted', 'sha256', 'size') if key in state} | {'job_id': result['job_id']}
            time.sleep(.1)
        raise RuntimeError('Frozen reference conversion exceeded test wait bound')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--root', required=True, type=Path)
    p.add_argument('command', choices=['health', 'auth', 'claim', 'devices', 'upload', 'jobs', 'assert-cleanup', 'cancel-pending', 'revoke', 'stop'])
    p.add_argument('--code')
    p.add_argument('--device')
    p.add_argument('--format', choices=['pdf', 'epub', 'docx', 'txt', 'md', 'all'], default='all')
    p.add_argument('--intentional-new', '--new-import', dest='new_import', action='store_true')
    p.add_argument('--expect-status', type=int, choices=[400, 401, 403, 404, 409, 410, 413, 422, 429])
    args = p.parse_args()
    os.umask(0o077)
    root = args.root.resolve()
    browser = Browser(root)
    if args.command == 'health':
        print(json.dumps(browser.call('GET', '/healthz')))
    elif args.command == 'auth':
        browser.login(); print(json.dumps({'authenticated': True, 'provider_configured': False}))
    elif args.command == 'claim':
        if not args.code:
            p.error('--code is required')
        result = browser.call('POST', '/v1/pair/claim', {'code': args.code})
        print(json.dumps({'claimed': result['state'] == 'claimed'}))
    elif args.command == 'devices':
        print(json.dumps(browser.call('GET', '/v1/devices')))
    elif args.command == 'upload':
        device = args.device
        if not device:
            active = [d for d in browser.call('GET', '/v1/devices')['devices'] if not d['revoked']]
            if len(active) != 1:
                raise RuntimeError('Specify exact synthetic device when active count is not one')
            device = active[0]['id']
        fixtures = browser.fixtures()
        formats = list(fixtures) if args.format == 'all' else [args.format]
        print(json.dumps({'jobs': [browser.upload(device, fixtures[fmt], args.new_import, args.expect_status) for fmt in formats]}, indent=2))
    elif args.command in ('jobs', 'assert-cleanup', 'cancel-pending'):
        jobs = browser.call('GET', '/v1/jobs')['jobs']
        if args.command == 'cancel-pending':
            canceled = 0
            for job in jobs:
                if job['status'] not in ('imported', 'deleted', 'canceled', 'expired'):
                    browser.call('DELETE', '/v1/jobs/' + job['job_id']); canceled += 1
            print(json.dumps({'canceled': canceled}))
        elif args.command == 'assert-cleanup':
            imported = [j for j in jobs if j['status'] == 'imported']
            if not imported:
                raise RuntimeError('No durable receiver ACKs to verify')
            for job in imported:
                for suffix in ('.upload', '.tmp', '.ready'):
                    if (root / 'data/private' / (job['job_id'] + suffix)).exists():
                        raise RuntimeError('Acknowledged synthetic relay blob remains')
            print(json.dumps({'imported_jobs': len(imported), 'relay_blobs_absent': True}))
        else:
            print(json.dumps({'jobs': [{key: j[key] for key in ('job_id', 'document_id', 'filename', 'source_format', 'format', 'status', 'converted', 'size')} for j in jobs]}, indent=2))
    elif args.command == 'revoke':
        if not args.device:
            p.error('--device is required for revoke')
        device = str(uuid.UUID(args.device))
        result = browser.call('DELETE', '/v1/devices/' + device)
        print(json.dumps({'device_id': device, 'revoked': result['revoked']}))
    elif args.command == 'stop':
        info = browser.info
        proc = Path('/proc') / str(info['pid'])
        if proc.joinpath('stat').read_text().split()[21] != info['start_ticks'] or b'reference_receiver_server.py' not in proc.joinpath('cmdline').read_bytes():
            raise RuntimeError('Test-owned server process identity changed')
        os.kill(info['pid'], signal.SIGTERM)
        print(json.dumps({'stop_requested_for_owned_server': True}))


if __name__ == '__main__':
    main()
