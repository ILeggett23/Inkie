import hashlib
import http.client
import json
import threading
import time
import uuid
from companion.db import password_hash
from companion.server import App, Server

class Client:
    def __init__(self, port, token=None):
        self.port, self.token, self.cookie, self.csrf = port, token, '', ''
    def call(self, method, path, data=None, raw=None, headers=None, expected=200):
        h={'Host':f'127.0.0.1:{self.port}'}
        if self.token: h['Authorization']='Bearer '+self.token
        if self.cookie: h['Cookie']=self.cookie
        if method!='GET':
            h['Origin']=f'http://127.0.0.1:{self.port}'
            h['X-CSRF-Token']=self.csrf
        if data is not None: h['Content-Type']='application/json';raw=json.dumps(data).encode()
        h.update(headers or {})
        conn=http.client.HTTPConnection('127.0.0.1',self.port,timeout=40)
        conn.request(method,path,body=raw,headers=h)
        response=conn.getresponse();body=response.read();rh=dict(response.getheaders());status=response.status;conn.close()
        if rh.get('Set-Cookie'): self.cookie=rh['Set-Cookie'].split(';')[0]
        result=json.loads(body) if rh.get('Content-Type','').startswith('application/json') else body
        if expected is not None and status!=expected: raise AssertionError(f'{method} {path}: expected {expected}, got {status}: {result}')
        if isinstance(result,dict) and 'csrf' in result:self.csrf=result['csrf']
        return result,status,rh

def start(root, api_key=''):
    app=App(root,'http://127.0.0.1:1',dev=True,api_key=api_key)
    server=Server(('127.0.0.1',0),app)
    port=server.server_address[1];app.origin=f'http://127.0.0.1:{port}';app.host=f'127.0.0.1:{port}'
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    return app,server,port

def account(app,port,email='test@example.invalid'):
    app.db.run('INSERT INTO users VALUES(?,?,?)',(str(uuid.uuid4()),email,password_hash('synthetic-test-password-123')))
    client=Client(port);client.call('POST','/v1/auth/login',{'email':email,'password':'synthetic-test-password-123'})
    return client

def pair(browser, name='Synthetic reader'):
    pairing=Client(browser.port)
    result=pairing.call('POST','/v1/pair/start',{'name':name},expected=201)[0]
    pairing.token=result['pairing_token']
    browser.call('POST','/v1/pair/claim',{'code':result['code']})
    pairing.call('GET','/v1/pair/'+result['pairing_id'])
    approval=pairing.call('POST','/v1/pair/'+result['pairing_id']+'/approve',{'approve':True},expected=201)[0]
    device=Client(browser.port,approval['device_token']);device.device_id=approval['device_id']
    return device,result

def upload(browser,device,name,data,new_import=False):
    result=browser.call('POST','/v1/jobs',{'device_id':device.device_id,'filename':name,'size':len(data),'sha256':hashlib.sha256(data).hexdigest(),'new_import':new_import},expected=None)[0]
    if result['status']=='uploading':
        ident=result['job_id']
        for start in range(result['received'],len(data),262144):
            block=data[start:start+262144]
            browser.call('PUT',f'/v1/jobs/{ident}/chunk',raw=block,headers={'Content-Type':'application/octet-stream','Content-Range':f'bytes {start}-{start+len(block)-1}/{len(data)}'})
        browser.call('POST',f'/v1/jobs/{ident}/complete',{})
    return result

def ready(browser,job_id,timeout=35):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        row=browser.call('GET','/v1/jobs/'+job_id)[0]
        if row['status'] in ('waiting for device','failed','imported','deleted'):
            return row
        time.sleep(.1)
    raise AssertionError('Document processing timeout')
