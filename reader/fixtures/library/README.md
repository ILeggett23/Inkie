# Authored library fixtures

These files contain only project-authored text and generated color squares. No private documents or downloads are used. They are separate from the preserved two-page qualification fixture.

- `authored-three-pages.pdf`: three pages, distinct headings, selectable text and one embedded image per page.
- `authored-three-chapters.epub`: EPUB 3 ZIP with three XHTML spine items, text and one PNG per chapter. Layout/page count is established by the actual reader parser in route tests.
- `malformed.pdf`, `malformed.epub`: invalid parser inputs.
- `traversal.epub`: otherwise valid EPUB with a `../escape.txt` member; reject before extraction.
- `oversized-entry.epub`: otherwise valid EPUB whose extra member declares 1 GiB uncompressed in both local and central ZIP headers while retaining a tiny compressed payload. This tests bounded metadata admission, not successful decompression of a large archive.
- `marker.png`: generated color marker.

Run `generate.py` with Python and ReportLab installed. The bundled document Python is sufficient. Fixed ZIP timestamps and ReportLab invariant mode make outputs reproducible; `MANIFEST.json` records every generated hash and expected heading. Two consecutive generations were byte-identical for all seven files. Pypdf validated all three PDF pages, heading text and embedded images; all three rendered PDF pages were visually checked. EPUB ZIP integrity was checked; application parsing and routing belong to `tests/library_route_tests.cpp` and must be reported separately.

License: AGPL-3.0-only, matching the project.
