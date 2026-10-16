"""Start the real service with synthetic queued documents for an external client.

Usage: python -m tests.serve_fixture --data /tmp/private-fixture --out /tmp/fixture.json
The output contains synthetic one-run device credentials, never commit it.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import threading
from .support import start,account,pair,upload,ready
from .fixtures import pdf,epub,docx

def main():
    p=argparse.ArgumentParser();p.add_argument('--data',required=True);p.add_argument('--out',required=True);args=p.parse_args()
    os.umask(0o077)
    app,server,port=start(args.data)
    browser=account(app,port)
    device,_=pair(browser)
    jobs=[]
    for name,data in [('synthetic.pdf',pdf()),('synthetic.epub',epub()),('synthetic.docx',docx()),('synthetic.txt',b'A simple synthetic text document.\n'),('synthetic.md',b'# Synthetic Markdown\n\nA **bold** sentence.')]:
        job=upload(browser,device,name,data);row=ready(browser,job['job_id'])
        if row['status']!='waiting for device':raise RuntimeError(row.get('error'))
        jobs.append({'job_id':job['job_id'],'document_id':job['document_id'],'format':row['format'],'source_format':row['source_format']})
    result={'base_url':f'http://127.0.0.1:{port}','device_id':device.device_id,'device_token':device.token,'jobs':jobs,'browser_account':'test@example.invalid','browser_password':'synthetic-test-password-123'}
    Path(args.out).write_text(json.dumps(result,indent=2)+'\n');os.chmod(args.out,0o600)
    print('Synthetic fixture is ready; credentials are only in the private output file.',flush=True)
    stop=threading.Event()
    signal.signal(signal.SIGTERM,lambda *_:stop.set());signal.signal(signal.SIGINT,lambda *_:stop.set())
    stop.wait();server.shutdown();server.server_close();app.close()

if __name__=='__main__':main()
