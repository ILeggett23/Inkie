"""Actual loopback HTTP, SQLite, file durability and converter integration.

Provider-specific tests use an explicitly simulated SSE transport and do not
qualify OpenAI credentials, model access, provider billing or deployed networking.
"""
import hashlib
import io
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import uuid
import zipfile

from companion.convert import convert, InvalidDocument, archive, xml, reader_css_ok
from companion.db import Database
from companion.tom import validated_request, provider_payload, TomError, parse_sse
from .support import Client,start,account,pair,upload,ready
from .fixtures import pdf,epub,docx

class HTTPIntegration(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.app,self.server,self.port=start(self.tmp.name)
        self.browser=account(self.app,self.port);self.device,self.pairing=pair(self.browser)
    def tearDown(self):
        self.server.shutdown();self.server.server_close();self.app.close();self.tmp.cleanup()
    def test_upload_checksum_worker_modules_are_served_as_javascript(self):
        import urllib.request
        for path, expected in [("hash-worker.js", "hashBlob"), ("sha256-stream.js", "class Sha256")]:
            with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/{path}") as response:
                self.assertEqual(response.status, 200)
                self.assertTrue(response.headers["Content-Type"].startswith("text/javascript"))
                self.assertIn(expected, response.read().decode("utf-8"))

    def test_all_formats_actual_worker_range_and_durable_ack(self):
        for name,data in [('sample.pdf',pdf()),('sample.epub',epub()),('sample.docx',docx()),('sample.txt',b'Synthetic UTF-8 text.\n'),('sample.md',b'# Title\n\n**Bold** synthetic text.')]:
            with self.subTest(name=name):
                job=upload(self.browser,self.device,name,data);row=ready(self.browser,job['job_id'])
                self.assertEqual(row['status'],'waiting for device',row.get('error'))
                manifest=self.device.call('GET','/v1/device/jobs')[0]['jobs'][0]
                self.assertFalse(manifest['intentional_new'])
                out=bytearray()
                while len(out)<manifest['size']:
                    end=min(len(out)+97,manifest['size'])-1
                    body,_,headers=self.device.call('GET',f"/v1/device/jobs/{manifest['job_id']}/content",headers={'Range':f'bytes={len(out)}-{end}','X-Download-Token':manifest['download_token']},expected=206)
                    self.assertEqual(headers['Content-Range'],f"bytes {len(out)}-{end}/{manifest['size']}");out.extend(body)
                self.assertEqual(hashlib.sha256(out).hexdigest(),manifest['sha256'])
                path=Path(self.tmp.name)/('durable.'+manifest['format']);path.write_bytes(out)
                convert(path,Path(self.tmp.name)/'validated.copy',manifest['format'],'synthetic')
                ack={'document_id':manifest['document_id'],'sha256':manifest['sha256'],'durable':True}
                self.device.call('POST',f"/v1/device/jobs/{manifest['job_id']}/ack",{**ack,'durable':False},expected=409)
                self.device.call('POST',f"/v1/device/jobs/{manifest['job_id']}/ack",ack)
                self.device.call('POST',f"/v1/device/jobs/{manifest['job_id']}/ack",ack)
                self.assertFalse(self.app.db.blob(manifest['job_id'],'.ready').exists())
                self.assertEqual(self.browser.call('GET','/v1/jobs/'+job['job_id'])[0]['status'],'imported')
                duplicate=upload(self.browser,self.device,name,data)
                self.assertEqual(duplicate['document_id'],manifest['document_id']);self.assertTrue(duplicate['duplicate'])
    def test_upload_resume_replay_offset_and_checksum_failure(self):
        data=b'A synthetic resumable file.\n'
        job=self.browser.call('POST','/v1/jobs',{'device_id':self.device.device_id,'filename':'resume.txt','size':len(data),'sha256':hashlib.sha256(data).hexdigest()},expected=201)[0];ident=job['job_id']
        headers={'Content-Range':f'bytes 0-9/{len(data)}'}
        self.browser.call('PUT',f'/v1/jobs/{ident}/chunk',raw=data[:10],headers=headers)
        again=self.browser.call('PUT',f'/v1/jobs/{ident}/chunk',raw=data[:10],headers=headers)[0];self.assertTrue(again['replayed'])
        self.browser.call('PUT',f'/v1/jobs/{ident}/chunk',raw=b'x'*10,headers=headers,expected=409)
        self.browser.call('POST',f'/v1/jobs/{ident}/complete',{},expected=409)
        self.browser.call('PUT',f'/v1/jobs/{ident}/chunk',raw=data[11:],headers={'Content-Range':f'bytes 11-{len(data)-1}/{len(data)}'},expected=409)
        recovered=upload(self.browser,self.device,'resume.txt',data);self.assertEqual(recovered['job_id'],ident)
        self.assertEqual(ready(self.browser,ident)['status'],'waiting for device')
        bad=self.browser.call('POST','/v1/jobs',{'device_id':self.device.device_id,'filename':'bad.txt','size':3,'sha256':'0'*64},expected=201)[0]
        self.browser.call('PUT',f"/v1/jobs/{bad['job_id']}/chunk",raw=b'bad',headers={'Content-Range':'bytes 0-2/3'})
        self.browser.call('POST',f"/v1/jobs/{bad['job_id']}/complete",{})
        row=ready(self.browser,bad['job_id']);self.assertEqual(row['status'],'failed');self.assertIn('checksum',row['error'])
    def test_tombstone_prevents_retry_and_explicit_new_import(self):
        data=b'Document for deletion test.';job=upload(self.browser,self.device,'trash.txt',data);ready(self.browser,job['job_id'])
        self.device.call('POST','/v1/device/tombstones',{'document_id':job['document_id']})
        self.assertEqual(self.device.call('GET','/v1/device/jobs')[0]['jobs'],[])
        self.device.call('POST',f"/v1/device/jobs/{job['job_id']}/ack",{'document_id':job['document_id'],'sha256':'0'*64,'durable':True},expected=409)
        self.browser.call('POST','/v1/jobs',{'device_id':self.device.device_id,'filename':'trash.txt','size':len(data),'sha256':hashlib.sha256(data).hexdigest()},expected=409)
        intentional=upload(self.browser,self.device,'trash.txt',data,True);self.assertNotEqual(intentional['document_id'],job['document_id']);self.assertEqual(ready(self.browser,intentional['job_id'])['status'],'waiting for device')
        self.assertTrue(self.device.call('GET','/v1/device/jobs')[0]['jobs'][0]['intentional_new'])
    def test_pairing_replay_expiry_and_revocation(self):
        token=Client(self.port,self.pairing['pairing_token'])
        token.call('POST','/v1/pair/'+self.pairing['pairing_id']+'/approve',{'approve':True},expected=409)
        self.browser.call('POST','/v1/pair/claim',{'code':self.pairing['code']},expected=409)
        self.app.db.run('UPDATE pairs SET expires=0 WHERE id=?',(self.pairing['pairing_id'],))
        token.call('GET','/v1/pair/'+self.pairing['pairing_id'],expected=410)
        self.browser.call('DELETE','/v1/devices/'+self.device.device_id)
        self.device.call('GET','/v1/device/jobs',expected=401)
    def test_every_object_private_csrf_and_origins(self):
        anonymous=Client(self.port);anonymous.call('GET','/v1/jobs',expected=401)
        other=account(self.app,self.port,'other@example.invalid');other_device,_=pair(other,'Other reader')
        job=upload(self.browser,self.device,'private.txt',b'Private synthetic fixture.');ready(self.browser,job['job_id'])
        other.call('GET','/v1/jobs/'+job['job_id'],expected=404)
        other.call('DELETE','/v1/devices/'+self.device.device_id,expected=404)
        other_device.call('GET',f"/v1/device/jobs/{job['job_id']}/content",expected=404)
        self.browser.call('DELETE','/v1/jobs/'+job['job_id'],headers={'X-CSRF-Token':'wrong'},expected=403)
        self.browser.call('POST','/v1/auth/logout',{},headers={'Origin':'https://evil.invalid'},expected=403)
        self.browser.call('GET','/v1/me',headers={'Host':'evil.invalid'},expected=403)
        self.device.call('GET',f"/v1/device/jobs/{job['job_id']}/content",expected=403)
        manifest=self.device.call('GET','/v1/device/jobs')[0]['jobs'][0]
        self.device.call('GET',f"/v1/device/jobs/{job['job_id']}/content",headers={'X-Download-Token':manifest['download_token']+'x'},expected=403)
        row=self.app.db.one('SELECT * FROM jobs WHERE id=?',(job['job_id'],));expired,_=self.app.ticket(row,self.device.device_id,int(time.time()-5))
        self.device.call('GET',f"/v1/device/jobs/{job['job_id']}/content",headers={'X-Download-Token':expired},expected=410)
    def test_disk_full_does_not_acknowledge_chunk(self):
        data=b'Synthetic file.';job=self.browser.call('POST','/v1/jobs',{'device_id':self.device.device_id,'filename':'full.txt','size':len(data),'sha256':hashlib.sha256(data).hexdigest()},expected=201)[0]
        with patch('companion.server.os.fsync',side_effect=OSError(28,'Synthetic disk full')):
            self.browser.call('PUT',f"/v1/jobs/{job['job_id']}/chunk",raw=data,headers={'Content-Range':f'bytes 0-{len(data)-1}/{len(data)}'},expected=500)
        self.assertEqual(self.browser.call('GET','/v1/jobs/'+job['job_id'])[0]['received'],0)
        upload(self.browser,self.device,'full.txt',data);self.assertEqual(ready(self.browser,job['job_id'])['status'],'waiting for device')
    def test_malformed_upload_names_and_active_docs_rejected(self):
        for name in ('../book.pdf','sub/book.pdf','a\\b.pdf','bad\x00.pdf'):
            self.browser.call('POST','/v1/jobs',{'device_id':self.device.device_id,'filename':name,'size':3,'sha256':'0'*64},expected=400)
        job=upload(self.browser,self.device,'fake.pdf',b'%PDF-invalid malicious data');self.assertEqual(ready(self.browser,job['job_id'])['status'],'failed')
        job=upload(self.browser,self.device,'binary.txt',b'\x00synthetic');self.assertEqual(ready(self.browser,job['job_id'])['status'],'failed')
    def test_expiration_cancellation_and_source_separation(self):
        data=b'Preserve the originating file.';original=Path(self.tmp.name)/'original.txt';original.write_bytes(data)
        job=upload(self.browser,self.device,'original.txt',data);ready(self.browser,job['job_id'])
        self.app.db.run('UPDATE jobs SET expires=0 WHERE id=?',(job['job_id'],));self.app.sweep()
        self.assertEqual(original.read_bytes(),data);self.assertFalse(self.app.db.blob(job['job_id'],'.ready').exists())
        new=upload(self.browser,self.device,'original.txt',data);self.assertNotEqual(new['job_id'],job['job_id'])
        self.browser.call('DELETE','/v1/jobs/'+new['job_id']);self.assertEqual(self.browser.call('GET','/v1/jobs/'+new['job_id'])[0]['status'],'canceled')
    def test_provider_unconfigured_no_draft_sent(self):
        payload=tom_request();self.device.call('POST','/v1/tom/requests',payload,expected=503)
        self.assertEqual(self.app.db.rows('SELECT * FROM tom'),[])
    def test_restart_retains_durable_chunks_and_queue(self):
        data=b'Restart-safe file.';job=upload(self.browser,self.device,'restart.txt',data);ready(self.browser,job['job_id'])
        self.server.shutdown();self.server.server_close();self.app.close()
        self.app,self.server,self.port=start(self.tmp.name)
        self.browser.port=self.port;self.device.port=self.port
        row=self.browser.call('GET','/v1/jobs/'+job['job_id'])[0];self.assertEqual(row['status'],'waiting for device')
        self.assertEqual(self.device.call('GET','/v1/device/jobs')[0]['jobs'][0]['document_id'],job['document_id'])
    def test_static_frontend_headers_and_relative_assets(self):
        html,status,headers=Client(self.port).call('GET','/')
        self.assertIn(b'./app.js',html);self.assertIn("frame-ancestors 'none'",headers['Content-Security-Policy'])
        self.assertEqual(headers['Cache-Control'],'no-store')
        self.assertNotIn(b'OPENAI_API_KEY=',html)

def tom_request():
    return {'request_id':str(uuid.uuid4()),'conversation_id':str(uuid.uuid4()),'model':'gpt-5.4-mini','scope':'general','context':None,'question':'A synthetic question','history':[],'consent':True,'provider':'openai','max_output_tokens':2048}

class DocumentSecurity(unittest.TestCase):
    def test_archive_path_and_bomb(self):
        for name,data in [('../escape',b'synthetic'),('large.txt',b'A'*100000)]:
            with tempfile.TemporaryDirectory() as temp:
                path=Path(temp)/'bad.zip'
                with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:z.writestr(name,data)
                with self.assertRaises(InvalidDocument):archive(path)
    def test_xml_entity_and_depth(self):
        with self.assertRaises(InvalidDocument):xml(b'<!DOCTYPE a [<!ENTITY a "bad">]><a>&a;</a>')
        with self.assertRaises(InvalidDocument):xml('<!DOCTYPE a [<!ENTITY a "bad">]><a>&a;</a>'.encode('utf-16'))
        with self.assertRaises(InvalidDocument):xml(('<a>'*100+'</a>'*100).encode())
    def test_docx_images_formatting_and_plain_escaping(self):
        with tempfile.TemporaryDirectory() as temp:
            src=Path(temp)/'a.docx';dst=Path(temp)/'a.epub';src.write_bytes(docx());convert(src,dst,'docx','fixture')
            with zipfile.ZipFile(dst) as z:
                text=z.read('EPUB/chapter.xhtml').decode();self.assertIn('<h1>',text);self.assertIn('<strong>',text);self.assertIn('<table>',text);self.assertIn('<img',text);self.assertIn('EPUB/image-0.png',z.namelist())
            src.write_text('<script>alert(1)</script>');convert(src,dst,'txt','safe')
            with zipfile.ZipFile(dst) as z:self.assertIn(b'&lt;script&gt;',z.read('EPUB/chapter.xhtml'))
    def test_pdf_active_content_removed_and_password_rejected(self):
        from pypdf import PdfReader, PdfWriter
        from pypdf.annotations import Link
        from pypdf.generic import NameObject, DictionaryObject, ArrayObject, NumberObject
        with tempfile.TemporaryDirectory() as temp:
            src=Path(temp)/'book.pdf';dst=Path(temp)/'output'
            # Real-world shape: a script, an automatic open action and a web link; all removed, pages kept.
            writer=PdfWriter();writer.add_blank_page(width=200,height=200);writer.add_blank_page(width=200,height=200);writer.add_js('app.alert("synthetic")')
            writer._root_object[NameObject('/OpenAction')]=ArrayObject([writer.pages[0].indirect_reference,NameObject('/Fit')])
            writer.add_annotation(page_number=0,annotation=Link(rect=(10,10,90,30),url='https://example.invalid/synthetic'))
            writer.add_annotation(page_number=0,annotation=Link(rect=(10,40,90,60),target_page_index=1))
            writer.write(src);result=convert(src,dst,'pdf','book');self.assertEqual(result['format'],'pdf')
            raw=dst.read_bytes();self.assertNotIn(b'/JavaScript',raw);self.assertNotIn(b'/OpenAction',raw);self.assertNotIn(b'example.invalid',raw)
            self.assertEqual(len(PdfReader(str(dst)).pages),2)
            writer=PdfWriter();writer.add_blank_page(width=20,height=20);writer.encrypt('synthetic-password');writer.write(src)
            with self.assertRaises(InvalidDocument):convert(src,dst,'pdf','bad')
            # An owner-password-only PDF (no password to open) is ordinary and accepted.
            writer=PdfWriter();writer.add_blank_page(width=20,height=20);writer.encrypt(user_password='',owner_password='synthetic-owner');writer.write(src)
            self.assertEqual(convert(src,dst,'pdf','open')['format'],'pdf')
    def test_real_world_epub_cleaned_not_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            src=Path(temp)/'book.epub';dst=Path(temp)/'output'
            with zipfile.ZipFile(src,'w') as z:
                z.writestr(zipfile.ZipInfo('mimetype'),b'application/epub+zip',compress_type=zipfile.ZIP_STORED)
                z.writestr('META-INF/container.xml','<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
                z.writestr('OEBPS/content.opf','<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">urn:uuid:synthetic</dc:identifier><dc:title>Synthetic</dc:title></metadata><manifest><item id="c1" href="chapter1.html" media-type="application/xhtml+xml"/><item id="css" href="style.css" media-type="text/css"/></manifest><spine><itemref idref="c1"/></spine></package>')
                z.writestr('OEBPS/chapter1.html','<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">\n<html xmlns="http://www.w3.org/1999/xhtml"><head><link rel="stylesheet" href="style.css"/></head><body><p>Synthetic&nbsp;text &mdash; <a href="https://example.invalid/book">source</a> <a href="#n1">note</a></p><p id="n1" style="quotes:\'\\201C\' \'\\201D\'">x</p></body></html>')
                z.writestr('OEBPS/style.css','@import url("https://example.invalid/remote.css");\nq:before{content:"\\201C"}\nbody{background:url(https://example.invalid/bg.png)}')
            result=convert(src,dst,'epub','book');self.assertEqual(result['format'],'epub')
            with zipfile.ZipFile(dst) as z:
                self.assertEqual(z.namelist()[0],'mimetype');chapter=z.read('OEBPS/chapter1.html').decode();css=z.read('OEBPS/style.css').decode()
                self.assertNotIn('DOCTYPE',chapter);self.assertIn('&#160;',chapter);self.assertNotIn('example.invalid',chapter);self.assertIn('href="#n1"',chapter)
                self.assertNotIn('@import',css);self.assertNotIn('example.invalid',css);self.assertIn('\u201c',css);self.assertTrue(reader_css_ok(css))
            # A declared entity (internal DTD subset) is still refused.
            with zipfile.ZipFile(src,'a') as z:z.writestr('OEBPS/evil.xhtml','<!DOCTYPE a [<!ENTITY e "x">]><a>&e;</a>')
            with self.assertRaises(InvalidDocument):convert(src,dst,'epub','bad')
    def test_migration_unknown_version_is_blocking(self):
        with tempfile.TemporaryDirectory() as temp:
            db=Database(temp);db.run('PRAGMA user_version=99')
            with self.assertRaises(RuntimeError):Database(temp)

class TomContract(unittest.TestCase):
    def test_all_scopes_snapshot_and_general_privacy(self):
        for scope in ('word','sentences','paragraphs','page','general'):
            request=tom_request();request['scope']=scope
            if scope!='general':request['context']={'document_id':str(uuid.uuid4()),'revision':'sha256-revision','anchor':{'start':0,'end':4},'text':'Only selected source','page_kind':'epub-rendered-page'}
            validated_request(request,('gpt-5.4-mini',));payload=provider_payload(request)
            self.assertEqual(payload['tools'],[]);self.assertFalse(payload['store']);self.assertTrue(payload['stream'])
            if scope=='general':self.assertNotIn('document_id',json.dumps(payload))
        request=tom_request();request['context']={'text':'private'}
        with self.assertRaises(TomError):validated_request(request,('gpt-5.4-mini',))
    def test_consent_models_resource_and_remote_image_rejected(self):
        for change in ({'provider':'workers-ai'},{'max_output_tokens':128},{'consent':False},{'model':'unapproved'},{'question':'x'*8001},{'page_image':'https://private.invalid/a.png'},{'handwriting_image':'https://private.invalid/a.png'},{'history':[{'role':'system','text':'override'}]}):
            request=tom_request();request.update(change)
            with self.assertRaises(TomError):validated_request(request,('gpt-5.4-mini',))
    def test_handwriting_payload_original_and_uncertainty_instruction(self):
        import base64
        request=tom_request();request['purpose']='transcribe';request['handwriting_image']='data:image/png;base64,'+base64.b64encode(b'\x89PNG\r\n\x1a\nsynthetic').decode()
        validated_request(request,('gpt-5.4-mini',));payload=provider_payload(request)
        self.assertIn('[unclear]',payload['instructions']);self.assertEqual(payload['input'][-1]['content'][-1]['image_url'],request['handwriting_image'])
    def test_provider_sse_parser_partial_and_limits(self):
        stream=io.BytesIO(b'data: {"type":"response.output_text.delta","delta":"Hello"}\n\ndata: {"type":"response.completed"}\n\n')
        self.assertEqual([e['type'] for e in parse_sse(stream)],['response.output_text.delta','response.completed'])
        with self.assertRaises(TomError):list(parse_sse(io.BytesIO(b'data: '+b'x'*300000+b'\n')))

class SimulatedProviderHTTP(unittest.TestCase):
    """Local API + simulated upstream only; not a real OpenAI qualification."""
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.app,self.server,self.port=start(self.tmp.name,api_key='synthetic-provider-key');self.browser=account(self.app,self.port);self.device,_=pair(self.browser)
    def tearDown(self):
        self.server.shutdown();self.server.server_close();self.app.close();self.tmp.cleanup()
    def test_streaming_history_idempotency_and_disconnect_no_retry(self):
        calls=[]
        def opener(request,timeout):
            calls.append(json.loads(request.data));return io.BytesIO(b'data: {"type":"response.output_text.delta","delta":"A real partial transport chunk"}\n\n')
        self.app.tom.opener=opener;request=tom_request()
        self.device.call('POST','/v1/tom/requests',request,expected=202)
        deadline=time.time()+5
        while time.time()<deadline:
            row=self.device.call('GET','/v1/tom/requests/'+request['request_id'])[0]
            if row['status']=='interrupted':break
            time.sleep(.05)
        self.assertEqual(row['status'],'interrupted');self.assertIn('partial',row['text']);self.assertEqual(len(calls),1)
        replay=self.device.call('POST','/v1/tom/requests',request,expected=202)[0];self.assertTrue(replay['replayed']);self.assertEqual(len(calls),1)
        events=self.device.call('GET','/v1/tom/requests/'+request['request_id']+'/events')[0];self.assertIn(b'event: delta',events)
        history=self.device.call('GET','/v1/tom/conversations/'+request['conversation_id'])[0];self.assertEqual(history['turns'][0]['text'],row['text'])
        self.device.call('POST','/v1/tom/requests',{**request,'question':'different'},expected=409)
        self.device.call('DELETE','/v1/tom/conversations/'+request['conversation_id']);self.assertEqual(self.device.call('GET','/v1/tom/conversations')[0]['conversations'],[])
    def test_cancel_preserves_partial_and_authorization(self):
        released=threading.Event()
        class SlowResponse(io.BytesIO):
            def __init__(self):super().__init__();self.count=0
            def readline(self,n=-1):
                self.count+=1
                if self.count==1:return b'data: {"type":"response.output_text.delta","delta":"Partial"}\n'
                if self.count==2:return b'\n'
                released.wait(3);return b''
            def close(self):released.set();super().close()
        self.app.tom.opener=lambda *_args,**_kw:SlowResponse();request=tom_request()
        self.device.call('POST','/v1/tom/requests',request,expected=202)
        deadline=time.time()+3
        while time.time()<deadline:
            row=self.device.call('GET','/v1/tom/requests/'+request['request_id'])[0]
            if row['text']:break
            time.sleep(.05)
        other=account(self.app,self.port,'other@example.invalid');other.call('POST','/v1/tom/requests/'+request['request_id']+'/cancel',{},expected=404)
        self.device.call('POST','/v1/tom/requests/'+request['request_id']+'/cancel',{})
        time.sleep(.1);row=self.device.call('GET','/v1/tom/requests/'+request['request_id'])[0]
        self.assertEqual(row['status'],'canceled');self.assertEqual(row['text'],'Partial')

if __name__=='__main__':unittest.main()
