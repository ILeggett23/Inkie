"""Ephemeral, single-conversion-at-a-time HTTP adapter; no public port exposure.
Cloudflare Container has outbound networking disabled. Caller sends bytes, never
URLs or executable commands. Persistent delivery state remains in D1/R2.
"""
from http.server import BaseHTTPRequestHandler, HTTPServer
import hashlib,json,os,pathlib,re,subprocess,sys,tempfile
from urllib.parse import unquote
MAX=64*1024*1024
ROOT=os.environ.get("INKBOUND_CONVERTER_ROOT","/app")
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def setup(self):super().setup();self.connection.settimeout(35)
    def do_GET(self):
        if self.path in ('/ping','/ready'):
            self.send_response(200);self.send_header('Content-Length','2');self.end_headers();self.wfile.write(b'OK')
        else:self.send_error(404)
    def do_POST(self):
        try:
            size=int(self.headers.get('Content-Length','0'));fmt=self.headers.get('X-Source-Format','');digest=self.headers.get('X-Source-SHA256','');name=unquote(self.headers.get('X-Source-Filename',''))
            if self.path!='/convert' or not 0<size<=MAX or fmt not in ('pdf','epub','docx','txt','md') or not re.fullmatch('[a-f0-9]{64}',digest) or len(name)>180 or re.search(r'[/\\\x00-\x1f]',name):raise ValueError('invalid input')
            with tempfile.TemporaryDirectory(prefix='convert-',dir='/tmp') as directory:
                source=pathlib.Path(directory)/'source';target=pathlib.Path(directory)/'result';checksum=hashlib.sha256();remaining=size
                with source.open('xb') as stream:
                    while remaining:
                        block=self.rfile.read(min(1024*1024,remaining))
                        if not block:raise ValueError('truncated input')
                        stream.write(block);checksum.update(block);remaining-=len(block)
                if checksum.hexdigest()!=digest:raise ValueError('checksum mismatch')
                child=subprocess.run([sys.executable,'-m','companion.convert',str(source),str(target),fmt,name],cwd=ROOT,env={'PATH':'/usr/local/bin:/usr/bin:/bin','LANG':'C.UTF-8','PYTHONDONTWRITEBYTECODE':'1','PYTHONPATH':ROOT},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=30,check=False)
                if child.returncode or len(child.stdout)>8192:
                    # The converter prints only fixed, content-free reasons (never text, names or parser internals).
                    try:reason=str(json.loads(child.stdout).get('error',''))[:200]
                    except Exception:reason=''
                    raise ValueError(reason or 'conversion rejected')
                result=json.loads(child.stdout)
                if result.get('format') not in ('pdf','epub') or not target.is_file() or not 0<target.stat().st_size<=MAX:raise ValueError('invalid result')
                with target.open('rb') as stream:output_digest=hashlib.file_digest(stream,'sha256').hexdigest()
                self.send_response(200);self.send_header('Content-Length',str(target.stat().st_size));self.send_header('Content-Type','application/octet-stream');self.send_header('X-Output-Format',result['format']);self.send_header('X-Output-SHA256',output_digest);self.end_headers()
                with target.open('rb') as stream:
                    while block:=stream.read(1024*1024):self.wfile.write(block)
        except (ValueError,TimeoutError,subprocess.TimeoutExpired,json.JSONDecodeError) as exc:
            reason=str(exc) if isinstance(exc,ValueError) and not isinstance(exc,json.JSONDecodeError) and str(exc) not in ('','conversion rejected','invalid input','truncated input','checksum mismatch','invalid result') else ''
            body=json.dumps({'error':reason or 'Input rejected or conversion unavailable'}).encode();self.send_response(422);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError):pass
        except Exception:
            body=b'Conversion failed';self.send_response(500);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
if __name__=='__main__':
    os.umask(0o077);HTTPServer((os.environ.get('CONVERTER_BIND','0.0.0.0'),int(os.environ.get('CONVERTER_PORT','8080'))),Handler).serve_forever()
