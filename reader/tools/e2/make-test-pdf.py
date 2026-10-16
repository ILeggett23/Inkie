#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Write the E2 phase-E test document: a small ruled PDF to write on (no dependencies)."""
import sys
from pathlib import Path

def page_stream(n):
    ops = ['0.6 G 0.5 w']
    for y in range(700, 80, -36):
        ops.append(f'72 {y} m 540 {y} l S')
    ops.append(f'BT /F1 18 Tf 72 740 Td (Inkbound E2 test page {n}) Tj ET')
    return '\n'.join(ops).encode()

def build(pages=3):
    objs = []
    kids = ' '.join(f'{4 + 2 * i} 0 R' for i in range(pages))
    objs.append(b'<< /Type /Catalog /Pages 2 0 R >>')
    objs.append(f'<< /Type /Pages /Kids [{kids}] /Count {pages} >>'.encode())
    objs.append(b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>')
    for i in range(pages):
        content = page_stream(i + 1)
        objs.append(f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents {5 + 2 * i} 0 R >>'.encode())
        objs.append(b'<< /Length ' + str(len(content)).encode() + b' >>\nstream\n' + content + b'\nendstream')
    out = bytearray(b'%PDF-1.4\n')
    offsets = []
    for i, body in enumerate(objs, 1):
        offsets.append(len(out))
        out += f'{i} 0 obj\n'.encode() + body + b'\nendobj\n'
    xref = len(out)
    out += f'xref\n0 {len(objs) + 1}\n0000000000 65535 f \n'.encode()
    for off in offsets:
        out += f'{off:010d} 00000 n \n'.encode()
    out += f'trailer\n<< /Size {len(objs) + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
    return bytes(out)

if __name__ == '__main__':
    Path(sys.argv[1]).write_bytes(build())
