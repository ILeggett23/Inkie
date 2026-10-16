"""Synthetic documents and accounts only; no reader implementation here."""
import io
import zipfile
from pypdf import PdfWriter

def pdf():
    writer = PdfWriter()
    writer.add_blank_page(width=600, height=800)
    from pypdf.generic import DecodedStreamObject, NameObject, DictionaryObject
    font = DictionaryObject({NameObject('/Type'):NameObject('/Font'),NameObject('/Subtype'):NameObject('/Type1'),NameObject('/BaseFont'):NameObject('/Helvetica')})
    page = writer.pages[0]
    page[NameObject('/Resources')] = DictionaryObject({NameObject('/Font'):DictionaryObject({NameObject('/F1'):writer._add_object(font)})})
    stream = DecodedStreamObject(); stream.set_data(b'BT /F1 20 Tf 50 720 Td (Synthetic Inkbound integration document.) Tj ET')
    page[NameObject('/Contents')] = writer._add_object(stream)
    output = io.BytesIO(); writer.write(output); return output.getvalue()

def epub():
    from companion.convert import epub as build
    import tempfile
    from pathlib import Path
    with tempfile.TemporaryDirectory() as tmp:
        dest = Path(tmp) / 'test.epub'
        build(dest, 'Synthetic reader contract fixture', '<h1>A small synthetic book</h1><p>This publication is generated for automated tests.</p>', {})
        return dest.read_bytes()

def docx():
    import base64
    output = io.BytesIO()
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml','<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="xml" ContentType="application/xml"/></Types>')
        z.writestr('word/document.xml','''<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><w:body><w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>Converted synthetic document</w:t></w:r></w:p><w:p><w:r><w:rPr><w:b/></w:rPr><w:t>Bold text with an embedded image.</w:t></w:r><w:r><w:drawing><a:blip r:embed="r1"/></w:drawing></w:r></w:p><w:tbl><w:tr><w:tc><w:p><w:r><w:t>Cell A</w:t></w:r></w:p></w:tc></w:tr></w:tbl></w:body></w:document>''')
        z.writestr('word/_rels/document.xml.rels','<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="r1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="media/image.png"/></Relationships>')
        from PIL import Image
        im = io.BytesIO(); Image.new('RGB',(20,20),(60,110,80)).save(im,format='PNG')
        z.writestr('word/media/image.png',im.getvalue())
    return output.getvalue()
