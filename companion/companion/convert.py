"""Bounded, offline document validation/conversion. Runs in a separate process.

No office automation, macros, shell commands, plugins, or resource downloads.
DOCX exports paragraphs, headings, emphasis, tables, and embedded raster images.
Unsupported content is rejected rather than silently represented as converted.
Real-world PDFs and EPUBs are cleaned instead: active content, web links and external
resources are removed and the remaining document is validated as before.
"""
import html
import io
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import sys
import zipfile
import xml.etree.ElementTree as ET
from urllib.parse import unquote

MAX_EXPANDED = 128 * 1024 * 1024
MAX_MEMBER = 24 * 1024 * 1024
MAX_TEXT = 2 * 1024 * 1024
MAX_ENTRIES = 2048
NS = {"w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main", "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships", "a": "http://schemas.openxmlformats.org/drawingml/2006/main"}

class InvalidDocument(ValueError):
    pass

def xml(data):
    if len(data) > MAX_MEMBER or b'\0' in data or re.search(br"<!\s*(DOCTYPE|ENTITY)", data, re.I):
        raise InvalidDocument("Unsafe or oversized XML")
    try:
        root = ET.fromstring(data)
    except ET.ParseError as exc:
        raise InvalidDocument("Malformed XML") from exc
    stack = [(root, 0)]
    count = 0
    while stack:
        node, depth = stack.pop()
        count += 1
        if count > 100000 or depth > 80:
            raise InvalidDocument("XML structure exceeds limits")
        stack.extend((child, depth + 1) for child in node)
    return root

def archive(path):
    z = zipfile.ZipFile(path)
    members = z.infolist()
    total = 0
    names = set()
    if len(members) > MAX_ENTRIES:
        raise InvalidDocument("Archive entry limit exceeded")
    for info in members:
        name = info.filename
        parts = PurePosixPath(name).parts
        if not name or name.startswith("/") or "\\" in name or any(p in ("..", ".") for p in parts) or ":" in name or "\x00" in name or name in names:
            raise InvalidDocument("Unsafe or duplicate archive path")
        names.add(name)
        if ((info.external_attr >> 16) & 0o170000) == 0o120000 or info.flag_bits & 1:
            raise InvalidDocument("Symlink or encrypted archive member")
        total += info.file_size
        if info.file_size > MAX_MEMBER or total > MAX_EXPANDED or info.file_size > max(info.compress_size, 1) * 200:
            raise InvalidDocument("Archive expansion limit exceeded")
    return z

def validate_pdf(source):
    from pypdf import PdfReader
    from pypdf.generic import IndirectObject, DictionaryObject, ArrayObject
    if source.read_bytes()[:5] != b"%PDF-":
        raise InvalidDocument("PDF signature mismatch")
    reader = PdfReader(str(source), strict=True)
    if reader.is_encrypted:
        raise InvalidDocument("Password-protected PDF is unsupported; provide an unlocked copy you may use")
    if not 1 <= len(reader.pages) <= 10000:
        raise InvalidDocument("PDF page count exceeds limits")
    forbidden = {"/JS", "/JavaScript", "/AA", "/OpenAction", "/Launch", "/EmbeddedFiles", "/EmbeddedFile", "/RichMedia", "/XFA", "/SubmitForm", "/ImportData", "/GoToR", "/GoToE", "/URI", "/Sound", "/Movie", "/Rendition"}
    stack = [(reader.trailer, 0)]
    seen = set()
    count = 0
    while stack:
        item, depth = stack.pop()
        count += 1
        if count > 250000 or depth > 100:
            raise InvalidDocument("PDF object graph exceeds limits")
        if isinstance(item, IndirectObject):
            key = (item.idnum, item.generation)
            if key in seen:
                continue
            seen.add(key)
            item = item.get_object()
        if isinstance(item, DictionaryObject):
            if any(str(k) in forbidden for k in item):
                raise InvalidDocument("Active or externally linked PDF content is unsupported")
            if str(item.get("/S", "")) in forbidden:
                raise InvalidDocument("Active PDF action is unsupported")
            stack.extend((v, depth + 1) for v in item.values())
        elif isinstance(item, ArrayObject):
            stack.extend((v, depth + 1) for v in item)
    return {"pages": len(reader.pages)}

def validate_epub(source):
    with archive(source) as z:
        if z.read("mimetype") != b"application/epub+zip":
            raise InvalidDocument("EPUB mimetype mismatch")
        container = xml(z.read("META-INF/container.xml"))
        paths = [n.attrib.get("full-path") for n in container.iter() if n.tag.endswith("}rootfile")]
        if not paths or any(p not in z.namelist() for p in paths):
            raise InvalidDocument("EPUB package missing")
        if "META-INF/encryption.xml" in z.namelist():
            raise InvalidDocument("Encrypted EPUB resources unsupported; no DRM circumvention")
        package = xml(z.read(paths[0]))
        if not any(n.tag.endswith("}spine") and len(n) for n in package.iter()):
            raise InvalidDocument("EPUB spine missing")
        for info in z.infolist():
            suffix = PurePosixPath(info.filename).suffix.lower()
            if suffix in (".js", ".exe", ".sh", ".wasm"):
                raise InvalidDocument("Active/unsupported EPUB resource")
            if suffix in (".xml", ".opf", ".xhtml", ".html", ".htm", ".svg", ".ncx"):
                root = xml(z.read(info))
                for node in root.iter():
                    tag = node.tag.rsplit("}", 1)[-1].lower()
                    if tag in ("script", "iframe", "object", "embed", "form", "foreignobject", "audio", "video", "animate", "set", "animatemotion", "animatetransform"):
                        raise InvalidDocument("Active EPUB element")
                    if tag == 'style':
                        validate_css(''.join(node.itertext()))
                    for key, val in node.attrib.items():
                        attr = key.rsplit("}", 1)[-1].lower()
                        if attr.startswith("on") or attr == "base":
                            raise InvalidDocument("Active EPUB attribute")
                        if attr in ("href", "src", "poster", "full-path"):
                            val = unquote(val)
                            if re.search(r"^[a-zA-Z][\w+.-]*:|^//|^/|\\", val):
                                raise InvalidDocument("External EPUB resources are unsupported")
                            local = val.split("#")[0].split("?")[0]
                            if local and posixpath.normpath(posixpath.join(posixpath.dirname(info.filename), local)).startswith("../"):
                                raise InvalidDocument("EPUB reference escapes archive")
                        if attr == "style":
                            validate_css(val)
            if suffix == ".css":
                validate_css(z.read(info).decode("utf-8"))
        return {"package": paths[0]}

def css_unescape(text):
    # CSS escapes (\201C, \") are ordinary in book style sheets; checks run on the decoded text.
    def one(m):
        hexa = m.group(1)
        if hexa:
            code = int(hexa, 16)
            return chr(code) if 0 < code <= 0x10FFFF and not 0xD800 <= code <= 0xDFFF else "\uFFFD"
        return m.group(2)
    return re.sub(r"\\(?:([0-9a-fA-F]{1,6})[ \t\r\n\f]?|(.))", one, text, flags=re.S)

def external_url(value):
    return bool(re.search(r"^[\w+.-]+:|^//|^/", value.strip(" \t\r\n\"'")))

def validate_css(text):
    # External fetching is forbidden, including through CSS escapes (checked after decoding them).
    decoded = css_unescape(text)
    if re.search(r"@import|expression\s*\(|javascript:|behavior\s*:|-moz-binding", decoded, re.I):
        raise InvalidDocument("Active/obfuscated CSS unsupported")
    for url in re.findall(r"url\s*\((.*?)\)", decoded, re.I):
        if external_url(url):
            raise InvalidDocument("External CSS resource unsupported")

def strip_at_rules(text):
    # @media/@supports blocks keep their inner rules; every other at-rule (@font-face, @page, @import,
    # @namespace, @charset, ...) is removed with its block or statement.
    out, i = [], 0
    while True:
        at = text.find("@", i)
        if at < 0:
            out.append(text[i:])
            return "".join(out)
        out.append(text[i:at])
        name = re.match(r"@([\w-]*)", text[at:]).group(1).lower()
        brace, semi = text.find("{", at), text.find(";", at)
        if brace < 0 or (0 <= semi < brace):
            i = len(text) if semi < 0 else semi + 1
            continue
        depth, j = 0, brace
        while j < len(text):
            depth += 1 if text[j] == "{" else -1 if text[j] == "}" else 0
            if depth == 0:
                break
            j += 1
        out.append(strip_at_rules(text[brace + 1:j]) if name in ("media", "supports") else "")
        i = j + 1

def reader_css_ok(text):
    # The Inkbound reader's own import rule (document_validation.cpp css()), checked here so a
    # delivered book is never refused at import on the tablet.
    s = text.lower()
    return not any(t in s for t in ("\0", "\\", "@", "url", "expression", "javascript:", "http:", "https:"))

def sanitize_css(text):
    """Style sheets the reader accepts: no comments, escapes written as characters, no at-rules (fonts,
    pages, imports) and no declaration that loads a resource. Anything still unsafe drops the sheet."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = css_unescape(text).replace("\\", "").replace("\0", "")
    text = strip_at_rules(text)
    text = re.sub(r"[^;{}]*(?:url\s*\(|expression\s*\(|javascript:|https?:)[^;{}]*;?", "", text, flags=re.I)
    text = text.replace("@", "")
    return text if reader_css_ok(text) else ""

def raster(data):
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = 16000000
    with Image.open(io.BytesIO(data)) as im:
        if im.format not in ("PNG", "JPEG") or im.width * im.height > 16000000:
            raise InvalidDocument("Only bounded embedded PNG/JPEG images are supported")
        im.verify()
    return data

def docx(source):
    media = {}
    with archive(source) as z:
        names = z.namelist()
        if "word/document.xml" not in names or "[Content_Types].xml" not in names:
            raise InvalidDocument("DOCX package missing")
        if any("vbaproject" in n.lower() or "/embeddings/" in n for n in names):
            raise InvalidDocument("Macros and embedded objects are unsupported")
        relationships = {}
        for name in names:
            if name.endswith(".rels"):
                for rel in xml(z.read(name)):
                    if rel.attrib.get("TargetMode", "").lower() == "external":
                        raise InvalidDocument("DOCX external resources are unsupported")
                    if name == "word/_rels/document.xml.rels":
                        target = rel.attrib.get("Target", "")
                        resolved = posixpath.normpath(posixpath.join("word", target))
                        if resolved.startswith("../") or resolved.startswith("/") or "\\" in resolved:
                            raise InvalidDocument("Unsafe relationship")
                        relationships[rel.attrib.get("Id")] = resolved
        root = xml(z.read("word/document.xml"))
        if any(n.tag.rsplit("}",1)[-1] in ("altChunk", "object", "fldSimple", "instrText") for n in root.iter()):
            raise InvalidDocument("DOCX fields/embedded content unsupported")
        def paragraph(p):
            pieces = []
            for node in p.iter():
                tag = node.tag.rsplit("}", 1)[-1]
                if tag == "r":
                    content = "".join(html.escape(t.text or "") if t.tag.endswith("}t") else "<br/>" if t.tag.endswith("}br") else " " if t.tag.endswith("}tab") else "" for t in node)
                    if node.find("w:rPr/w:b", NS) is not None:
                        content = "<strong>" + content + "</strong>"
                    if node.find("w:rPr/w:i", NS) is not None:
                        content = "<em>" + content + "</em>"
                    pieces.append(content)
                elif tag == "blip":
                    target = relationships.get(node.attrib.get("{" + NS["r"] + "}embed"))
                    if not target or target not in names:
                        raise InvalidDocument("Missing embedded DOCX image")
                    ext = PurePosixPath(target).suffix.lower()
                    if ext not in (".png", ".jpg", ".jpeg"):
                        raise InvalidDocument("DOCX image format unsupported")
                    image_name = "image-" + str(len(media)) + ext
                    media[image_name] = raster(z.read(target))
                    pieces.append('<img src="' + image_name + '" alt="Embedded illustration"/>')
            style = p.find("w:pPr/w:pStyle", NS)
            value = style.attrib.get("{" + NS["w"] + "}val", "") if style is not None else ""
            tag = "h" + value[-1] if re.fullmatch(r"Heading[1-6]", value, re.I) else "p"
            return "<" + tag + ">" + "".join(pieces) + "</" + tag + ">"
        body = root.find("w:body", NS)
        if body is None:
            raise InvalidDocument("DOCX body missing")
        output = []
        for element in body:
            if element.tag.endswith("}p"):
                output.append(paragraph(element))
            elif element.tag.endswith("}tbl"):
                output.append("<table>")
                for row in element.findall("w:tr", NS):
                    output.append("<tr>" + "".join("<td>" + "".join(paragraph(p) for p in cell.findall("w:p", NS)) + "</td>" for cell in row.findall("w:tc", NS)) + "</tr>")
                output.append("</table>")
        return "".join(output), media

def plain(source, markdown):
    data = source.read_bytes()
    if len(data) > MAX_TEXT or b"\0" in data:
        raise InvalidDocument("Plain text exceeds 2 MiB or contains binary data")
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        raise InvalidDocument("Text must be UTF-8") from exc
    out = []
    code = False
    for line in text.splitlines():
        if markdown and line.startswith("```"):
            code = not code
            out.append("<pre>" if code else "</pre>")
            continue
        escaped = html.escape(line)
        if code:
            out.append(escaped + "\n")
            continue
        if markdown:
            heading = re.match(r"^(#{1,6}) (.*)$", escaped)
            if heading:
                n = str(len(heading[1]))
                out.append("<h" + n + ">" + heading[2] + "</h" + n + ">")
                continue
            escaped = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", escaped)
            escaped = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", escaped)
            escaped = re.sub(r"`([^`]+)`", r"<code>\1</code>", escaped)
            if escaped.startswith(("- ", "* ")):
                escaped = "• " + escaped[2:]
        out.append("<p>" + escaped + "</p>")
    if code:
        out.append("</pre>")
    return "".join(out), {}

def epub(destination, title, body, media):
    title = html.escape(title)
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="EPUB/package.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        image_items = "".join('<item id="i' + str(i) + '" href="' + name + '" media-type="image/' + ("png" if name.endswith(".png") else "jpeg") + '"/>' for i, name in enumerate(media))
        z.writestr("EPUB/package.opf", '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">urn:inkbound:converted</dc:identifier><dc:title>' + title + '</dc:title><dc:language>en</dc:language><meta property="dcterms:modified">2026-09-13T00:00:00Z</meta></metadata><manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>' + image_items + '</manifest><spine><itemref idref="chapter"/></spine></package>')
        z.writestr("EPUB/chapter.xhtml", '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>' + title + '</title><style>body{line-height:1.55}img{max-width:100%;height:auto}table{border-collapse:collapse}td{border:1px solid;padding:.3em}pre{white-space:pre-wrap}</style></head><body>' + body + '</body></html>')
        z.writestr("EPUB/nav.xhtml", '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head><body><nav epub:type="toc"><ol><li><a href="chapter.xhtml">' + title + '</a></li></ol></nav></body></html>')
        for name, data in media.items():
            z.writestr("EPUB/" + name, data)

ACTIVE_PDF = {"/JS", "/JavaScript", "/AA", "/OpenAction", "/Launch", "/EmbeddedFiles", "/EmbeddedFile", "/RichMedia", "/XFA", "/SubmitForm", "/ImportData", "/GoToR", "/GoToE", "/URI", "/Sound", "/Movie", "/Rendition"}
ACTIVE_ANNOTATIONS = {"/FileAttachment", "/RichMedia", "/Sound", "/Movie", "/Screen"}

def sanitize_pdf(source, destination):
    """Copy a PDF without scripts, automatic/launch/web actions, attachments or media; then validate the copy."""
    from pypdf import PdfReader, PdfWriter
    from pypdf.generic import IndirectObject, DictionaryObject, ArrayObject
    if source.read_bytes()[:5] != b"%PDF-":
        raise InvalidDocument("PDF signature mismatch")
    try:
        reader = PdfReader(str(source), strict=False)
        if reader.is_encrypted and not reader.decrypt(""):
            raise InvalidDocument("Password-protected PDF is unsupported; provide an unlocked copy you may use")
        pages = len(reader.pages)
    except InvalidDocument:
        raise
    except Exception as exc:
        raise InvalidDocument("PDF could not be read") from exc
    if not 1 <= pages <= 10000:
        raise InvalidDocument("PDF page count exceeds limits")
    writer = PdfWriter(clone_from=reader)
    def active(value):
        value = value.get_object() if isinstance(value, IndirectObject) else value
        return isinstance(value, DictionaryObject) and (str(value.get("/S", "")) in ACTIVE_PDF or str(value.get("/Subtype", "")) in ACTIVE_ANNOTATIONS)
    stack, seen, count = [writer._root_object], set(), 0
    while stack:
        item = stack.pop()
        count += 1
        if count > 500000:
            raise InvalidDocument("PDF object graph exceeds limits")
        if isinstance(item, IndirectObject):
            key = (item.idnum, item.generation)
            if key in seen:
                continue
            seen.add(key)
            item = item.get_object()
        if isinstance(item, DictionaryObject):
            for key in list(item.keys()):
                value = item.raw_get(key)
                if str(key) in ACTIVE_PDF or active(value):
                    del item[key]
                else:
                    stack.append(value)
        elif isinstance(item, ArrayObject):
            for index in range(len(item) - 1, -1, -1):
                if active(item[index]):
                    del item[index]
                else:
                    stack.append(item[index])
    writer.compress_identical_objects(remove_identicals=False, remove_orphans=True)  # unreferenced (removed) objects are not written
    with destination.open("wb") as stream:
        writer.write(stream)
    return validate_pdf(destination)

XML_ENTITIES = {"amp", "lt", "gt", "quot", "apos"}

def sanitize_markup(text):
    """Book markup without DOCTYPE declarations, with numeric entities and with web links/external resources neutralized."""
    if re.search(r"<!\s*ENTITY|<!\s*DOCTYPE[^>]*\[", text, re.I):
        raise InvalidDocument("Unsafe or oversized XML")
    text = re.sub(r"<!\s*DOCTYPE[^>]*>", "", text, flags=re.I)
    text = re.sub(r"&([A-Za-z][A-Za-z0-9]*);", lambda m: m.group(0) if m.group(1) in XML_ENTITIES or m.group(1) not in html.entities.name2codepoint else "&#%d;" % html.entities.name2codepoint[m.group(1)], text)
    def attribute(m):
        name, quote, value = m.group(1), m.group(2), m.group(3)
        if not re.search(r"^[a-zA-Z][\w+.-]*:|^//|^/|\\", unquote(html.unescape(value))):
            return m.group(0)
        return "%s=%s%s%s" % (name, quote, "#" if name.rsplit(":", 1)[-1].lower() == "href" else "", quote)
    text = re.sub(r"(\s(?:[\w-]+:)?(?:href|src|poster))\s*=\s*([\"'])(.*?)\2", attribute, text, flags=re.I | re.S)
    text = re.sub(r"(<style\b[^>]*>)(.*?)(</style>)", lambda m: m.group(1) + sanitize_css(m.group(2)) + m.group(3), text, flags=re.I | re.S)
    return re.sub(r"(\sstyle\s*=\s*)([\"'])(.*?)\2", lambda m: m.group(1) + m.group(2) + sanitize_css(m.group(3)) + m.group(2), text, flags=re.I | re.S)

def sanitize_epub(source, destination):
    """Rewrite an EPUB with cleaned markup and style sheets (same members and order), then validate the copy."""
    with archive(source) as z:
        names = z.namelist()
        if "mimetype" not in names or z.read("mimetype") != b"application/epub+zip":
            raise InvalidDocument("EPUB mimetype mismatch")
        if "META-INF/encryption.xml" in names:
            raise InvalidDocument("Encrypted EPUB resources unsupported; no DRM circumvention")
        with zipfile.ZipFile(destination, "w") as out:
            out.writestr(zipfile.ZipInfo("mimetype"), b"application/epub+zip", compress_type=zipfile.ZIP_STORED)
            for info in z.infolist():
                if info.filename == "mimetype" or info.is_dir():
                    continue
                data = z.read(info)
                suffix = PurePosixPath(info.filename).suffix.lower()
                if suffix in (".xml", ".opf", ".xhtml", ".html", ".htm", ".svg", ".ncx", ".css"):
                    try:
                        text = data.decode("utf-8-sig")
                    except UnicodeDecodeError as exc:
                        raise InvalidDocument("EPUB text is not UTF-8") from exc
                    data = (sanitize_css(text) if suffix == ".css" else sanitize_markup(text)).encode("utf-8")
                out.writestr(zipfile.ZipInfo(info.filename, date_time=info.date_time), data, compress_type=zipfile.ZIP_DEFLATED)
    return validate_epub(destination)

def convert(source, destination, fmt, title):
    source, destination = Path(source), Path(destination)
    if fmt == "pdf":
        info = sanitize_pdf(source, destination)
    elif fmt == "epub":
        info = sanitize_epub(source, destination)
    elif fmt in ("docx", "txt", "md"):
        body, media = docx(source) if fmt == "docx" else plain(source, fmt == "md")
        epub(destination, title, body, media)
        info = validate_epub(destination)
    else:
        raise InvalidDocument("Unsupported document format")
    os.chmod(destination, 0o600)
    with destination.open("rb") as f:
        os.fsync(f.fileno())
    return {"format": fmt if fmt in ("pdf", "epub") else "epub", "converted": fmt not in ("pdf", "epub"), "details": info}

def main():
    # Network-free Python worker plus Unix process resource limits. Deployment
    # additionally confines filesystem/capabilities through the container.
    import resource
    import socket
    resource.setrlimit(resource.RLIMIT_CPU, (20, 20))
    resource.setrlimit(resource.RLIMIT_FSIZE, (160 * 1024 * 1024,) * 2)
    if sys.platform.startswith("linux"):
        resource.setrlimit(resource.RLIMIT_AS, (768 * 1024 * 1024,) * 2)
    def no_network(*args, **kwargs):
        raise PermissionError("Conversion workers cannot use network sockets")
    socket.socket = no_network
    try:
        result = convert(*sys.argv[1:5])
        print(json.dumps(result))
    except Exception as exc:
        # Never echo source text, filenames, parser internals, or environment.
        message = str(exc) if isinstance(exc, InvalidDocument) else "Document parser rejected this file"
        print(json.dumps({"error": message}))
        raise SystemExit(2)

if __name__ == "__main__":
    main()
