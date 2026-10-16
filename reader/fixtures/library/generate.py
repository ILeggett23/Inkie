#!/usr/bin/env python3
"""Deterministic, project-authored parser fixtures. No downloads/private documents.

Requires reportlab (PDF only); other files use Python's standard library.
Run with the bundled document Python or any Python with reportlab installed.
Oversize EPUB declares a 1 GiB entry but contains only a few compressed bytes:
validation must reject its ZIP metadata before attempting document extraction.
"""
from pathlib import Path
import hashlib
import io
import json
import struct
import zipfile
import zlib
from reportlab.pdfgen import canvas
from reportlab.lib.utils import ImageReader

ROOT = Path(__file__).resolve().parent
STAMP = (2020, 1, 1, 0, 0, 0)
TEXTS = [
    ('ORCHARD ALPHA', 'The orchard keeps three quiet paths. A red square marks the first page.'),
    ('RIVER BRAVO', 'The river passes a stone bridge. A green square marks the second page.'),
    ('SUMMIT CHARLIE', 'The summit holds a clear view. A blue square marks the third page.'),
]


def png(red, green, blue):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + bytes((red, green, blue)) * 16 for _ in range(16))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 16, 16, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def epub(extra=()):
    files = [
        ('mimetype', b'application/epub+zip', zipfile.ZIP_STORED),
        ('META-INF/container.xml', b'''<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="EPUB/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>''', zipfile.ZIP_DEFLATED),
    ]
    manifest = ''.join(f'<item id="c{i}" href="chapter{i}.xhtml" media-type="application/xhtml+xml"/>'
                       f'<item id="im{i}" href="image{i}.png" media-type="image/png"/>' for i in range(1, 4))
    spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(1, 4))
    opf = f'''<?xml version="1.0" encoding="utf-8"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="book"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="book">urn:uuid:5afcf81f-b4b1-4552-9a61-9e89dce17d365</dc:identifier><dc:title>Authored Library Trails</dc:title><dc:language>en</dc:language><dc:creator>Inkbound test authors</dc:creator><meta property="dcterms:modified">2020-01-01T00:00:00Z</meta></metadata><manifest>{manifest}<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest><spine>{spine}</spine></package>'''
    files.append(('EPUB/content.opf', opf.encode(), zipfile.ZIP_DEFLATED))
    nav = '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head><body><nav epub:type="toc"><ol>' + ''.join(f'<li><a href="chapter{i}.xhtml">Chapter {i}</a></li>' for i in range(1, 4)) + '</ol></nav></body></html>'
    files.append(('EPUB/nav.xhtml', nav.encode(), zipfile.ZIP_DEFLATED))
    for i, (title, body) in enumerate(TEXTS, 1):
        page = f'''<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>{title}</title><style>body {{ font-family: serif; }} img {{ width: 64px; height: 64px; }}</style></head><body><h1>{title}</h1><p>{body}</p><p>Authored fixture chapter {i} of 3.</p><img src="image{i}.png" alt="Chapter color marker"/></body></html>'''
        files.append((f'EPUB/chapter{i}.xhtml', page.encode(), zipfile.ZIP_DEFLATED))
        files.append((f'EPUB/image{i}.png', png(*(255 if j == i - 1 else 30 for j in range(3))), zipfile.ZIP_DEFLATED))
    files.extend(extra)
    data = io.BytesIO()
    with zipfile.ZipFile(data, 'w') as book:
        for name, content, compression in files:
            info = zipfile.ZipInfo(name, STAMP)
            info.compress_type = compression
            info.external_attr = 0o100644 << 16
            info.create_system = 3
            book.writestr(info, content)
    return data.getvalue()


def main():
    image = png(220, 35, 35)
    pdf = io.BytesIO()
    doc = canvas.Canvas(pdf, pagesize=(612, 792), invariant=1, pageCompression=1)
    doc.setTitle('Authored Library Trails: Three Pages')
    doc.setAuthor('Inkbound test authors')
    for i, (title, body) in enumerate(TEXTS, 1):
        doc.setFont('Helvetica-Bold', 22)
        doc.drawString(54, 712, title)
        doc.setFont('Helvetica', 13)
        # Deliberate short lines keep the fixture visible in all target renderers.
        words = body.split(); lines = [' '.join(words[:8]), ' '.join(words[8:])]
        for j, line in enumerate(lines):
            doc.drawString(54, 668 - j * 22, line)
        doc.drawImage(ImageReader(io.BytesIO(png(*(255 if j == i - 1 else 30 for j in range(3))))), 54, 530, 64, 64)
        doc.drawString(54, 470, f'Authored fixture page {i} of 3.')
        doc.showPage()
    doc.save()
    valid = epub()
    oversized = bytearray(epub([('EPUB/oversized.bin', b'x', zipfile.ZIP_DEFLATED)]))
    # Alter only the entry's local/central uncompressed size: no large allocation
    # or accidental decompression bomb is needed to exercise metadata admission.
    offset = 0
    while True:
        offset = oversized.find(b'PK\x01\x02', offset)
        if offset < 0:
            break
        nlen = struct.unpack_from('<H', oversized, offset + 28)[0]
        if oversized[offset + 46:offset + 46 + nlen] == b'EPUB/oversized.bin':
            struct.pack_into('<I', oversized, offset + 24, 1024 * 1024 * 1024)
            local = struct.unpack_from('<I', oversized, offset + 42)[0]
            struct.pack_into('<I', oversized, local + 22, 1024 * 1024 * 1024)
            break
        offset += 4
    files = {
        'authored-three-pages.pdf': pdf.getvalue(),
        'authored-three-chapters.epub': valid,
        'malformed.pdf': b'%PDF-1.7\nnot a PDF object graph\n%%EOF\n',
        'malformed.epub': b'PK\x03\x04broken zip central directory',
        'traversal.epub': epub([('../escape.txt', b'Never extract outside the staged import.', zipfile.ZIP_DEFLATED)]),
        'oversized-entry.epub': bytes(oversized),
        'marker.png': image,
    }
    for name, data in files.items():
        (ROOT / name).write_bytes(data)
    manifest = {'origin': 'Project-authored synthetic fixtures; no private documents or downloads',
                'license': 'AGPL-3.0-only', 'pdf_pages': 3, 'epub_spine_items': 3,
                'expected_text': [title for title, _ in TEXTS],
                'oversized_entry_declared_bytes': 1024 * 1024 * 1024,
                'files': {name: {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()} for name, data in files.items()}}
    (ROOT / 'MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
