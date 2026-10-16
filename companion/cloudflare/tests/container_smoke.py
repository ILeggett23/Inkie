"""Synthetic loopback smoke test, suitable for `docker exec -i NAME python -`.

Run the converter container as uid 10001 with --network none; feed this source
through stdin. Runtime dependencies are already in the image. This does not
contact any public service or copy private documents into the image.
"""
import hashlib
import io
import json
import os
import time
import urllib.error
import urllib.request
import zipfile
from pypdf import PdfReader, PdfWriter

BASE = 'http://127.0.0.1:8080'
assert os.getuid() == 10001, 'converter smoke must run as the image non-root uid'
# Explicitly ignore host/container proxy environment variables.
client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
for attempt in range(100):
    try:
        with client.open(BASE + '/ready', timeout=2) as reply:
            if reply.read() == b'OK':
                break
    except (OSError, urllib.error.URLError):
        time.sleep(0.1)
else:
    raise AssertionError('converter did not become ready')

def zipped(files):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, content in files.items():
            archive.writestr(name, content)
    return output.getvalue()

writer = PdfWriter()
writer.add_blank_page(width=600, height=800)
pdf_bytes = io.BytesIO()
writer.write(pdf_bytes)
epub_bytes = zipped({
    'mimetype': 'application/epub+zip',
    'META-INF/container.xml': '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
    'book.opf': '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">synthetic-container-test</dc:identifier><dc:title>Synthetic fixture</dc:title><dc:language>en</dc:language></metadata><manifest><item id="page" href="page.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="page"/></spine></package>',
    'page.xhtml': '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Synthetic</title></head><body><p>Synthetic container fixture.</p></body></html>',
})
docx_bytes = zipped({
    '[Content_Types].xml': '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="xml" ContentType="application/xml"/></Types>',
    'word/document.xml': '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:r><w:t>Synthetic converted text.</w:t></w:r></w:p><w:tbl><w:tr><w:tc><w:p><w:r><w:t>Synthetic table cell.</w:t></w:r></w:p></w:tc></w:tr></w:tbl></w:body></w:document>',
})
passed = []
def convert(label, fmt, content, expected=200, changes=None):
    headers = {'Content-Length': str(len(content)), 'X-Source-Format': fmt,
               'X-Source-SHA256': hashlib.sha256(content).hexdigest(),
               'X-Source-Filename': 'synthetic.' + fmt}
    headers.update(changes or {})
    request = urllib.request.Request(BASE + '/convert', data=content, method='POST', headers=headers)
    try:
        reply = client.open(request, timeout=40)
    except urllib.error.HTTPError as error:
        reply = error
    with reply:
        result = reply.read(64 * 1024 * 1024 + 1)
        assert reply.status == expected, (label, reply.status, result[:120])
        assert int(reply.headers['Content-Length']) == len(result), label
        if expected == 200:
            assert len(result) <= 64 * 1024 * 1024, label
            assert hashlib.sha256(result).hexdigest() == reply.headers['X-Output-SHA256'], label
            output_format = reply.headers['X-Output-Format']
            assert output_format == ('pdf' if fmt == 'pdf' else 'epub'), label
            if output_format == 'pdf':
                assert len(PdfReader(io.BytesIO(result), strict=True).pages) == 1
            else:
                with zipfile.ZipFile(io.BytesIO(result)) as archive:
                    assert archive.read('mimetype') == b'application/epub+zip'
                    assert any(n.endswith('.xhtml') for n in archive.namelist())
        passed.append(label)

for fmt, content in [('pdf', pdf_bytes.getvalue()), ('epub', epub_bytes), ('docx', docx_bytes),
                     ('txt', b'Synthetic text fixture.'), ('md', b'# Synthetic heading\n\n**Synthetic note.**')]:
    convert('valid-' + fmt, fmt, content)
convert('wrong-checksum', 'txt', b'wrong digest', 422, {'X-Source-SHA256': '0' * 64})
convert('malformed-pdf', 'pdf', b'not a PDF', 422)
convert('archive-traversal', 'docx', zipped({'../escape': 'synthetic'}), 422)
convert('invalid-filename', 'txt', b'synthetic', 422, {'X-Source-Filename': '../escape.txt'})
convert('oversized-declaration', 'txt', b'x', 422, {'Content-Length': str(64 * 1024 * 1024 + 1)})
print(json.dumps({'passed': len(passed), 'cases': passed, 'uid': os.getuid(), 'network': 'loopback only; runtime network isolation must be inspected separately'}))
