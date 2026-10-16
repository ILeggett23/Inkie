#include "core/document/MuPdfDocumentEngine.h"
#include "core/document/PageFit.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <fstream>
#include <chrono>
#include <utility>
using namespace inkbound::document;
static void require(bool value, const char* label) {
  if (!value) throw std::runtime_error(label);
  std::cout << label << "=PASS\n";
}

#if defined(INKBOUND_HAS_MUPDF)
// Self-contained, stored ZIP fixture. Only synthetic text and a generated PNG.
static void u16(std::string& s, unsigned n) {
  s.push_back(static_cast<char>(n)); s.push_back(static_cast<char>(n >> 8));
}
static void u32(std::string& s, unsigned n) { u16(s,n); u16(s,n >> 16); }
static unsigned crc(const std::string& s) {
  unsigned n=0xffffffffU;
  for (unsigned char c:s) {n^=c; for(int i=0;i<8;++i) n=(n>>1)^(0xedb88320U & (0U-(n&1U)));}
  return ~n;
}
static void makeEpub(const std::filesystem::path& path, bool image) {
  std::string text;
  for(int i=0;i<160;++i) text += "<p>Reader integration tests preserve text across EPUB layout and pagination.</p>";
  std::vector<std::pair<std::string,std::string>> entries{
    {"mimetype","application/epub+zip"},
    {"META-INF/container.xml",R"(<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>)"},
    {"OEBPS/book.opf",R"(<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">synthetic-reader-test</dc:identifier><dc:title>Reader integration</dc:title><dc:language>en</dc:language></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/><item id="image" href="image.png" media-type="image/png"/></manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>)"},
    {"OEBPS/a.xhtml",std::string(R"(<html xmlns="http://www.w3.org/1999/xhtml"><head><title>First</title></head><body>)") +
       (image ? "<img src=\"image.png\" width=\"100\" height=\"100\"/>" : "") + text + "</body></html>"},
    {"OEBPS/b.xhtml",R"(<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Second</title></head><body><p>Second spine item end marker.</p></body></html>)"},
    {"OEBPS/image.png",std::string("\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x02\x00\x00\x00\x90\x77\x53\xde\x00\x00\x00\x0c\x49\x44\x41\x54\x78\x9c\x63\x60\x60\x60\x00\x00\x00\x04\x00\x01\xf6\x17\x38\x55\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82",69)}
  };
  std::string zip,central;
  for (const auto& [name,data]:entries) {
    unsigned offset=static_cast<unsigned>(zip.size()), checksum=crc(data);
    u32(zip,0x04034b50);u16(zip,20);u16(zip,0);u16(zip,0);u16(zip,0);u16(zip,0);
    u32(zip,checksum);u32(zip,data.size());u32(zip,data.size());u16(zip,name.size());u16(zip,0);
    zip+=name;zip+=data;
    u32(central,0x02014b50);u16(central,20);u16(central,20);u16(central,0);u16(central,0);u16(central,0);u16(central,0);
    u32(central,checksum);u32(central,data.size());u32(central,data.size());u16(central,name.size());
    u16(central,0);u16(central,0);u16(central,0);u16(central,0);u32(central,0);u32(central,offset);central+=name;
  }
  auto offset=zip.size();zip+=central;
  u32(zip,0x06054b50);u16(zip,0);u16(zip,0);u16(zip,entries.size());u16(zip,entries.size());
  u32(zip,central.size());u32(zip,offset);u16(zip,0);
  std::ofstream out(path,std::ios::binary);out.write(zip.data(),zip.size());
  if(!out) throw std::runtime_error("fixture-write");
}
static void epubRegressions(MuPdfDocumentEngine& engine) {
  auto dir=std::filesystem::temp_directory_path()/(
      "reader-epub-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(dir);
  struct Cleanup { std::filesystem::path p; ~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);} } cleanup{dir};
  makeEpub(dir/"image.epub",true);makeEpub(dir/"text.epub",false);
  engine.closeDocument();
  require(!engine.setEpubLayout({0,800,12}) && engine.setEpubLayout({600,800,12}),"epub_layout_validation");
  require(engine.openDocument(dir/"image.epub") && engine.documentMetadata().format=="EPUB","epub_open_format");
  auto count=engine.locationCount();auto identity=engine.layoutIdentity();
  require(count>2 && identity=="mupdf-1.28.3-epub-v1-w600-h800-em12","epub_layout_count_identity");
  require(!engine.setEpubLayout({600,400,12}),"epub_open_layout_immutable");
  auto bounds=engine.pageBounds(0);auto first=engine.renderPage(0,1,true);
  require(bounds && bounds->width==600 && bounds->height==800 && first &&
          first->pixelSize.width==600 && first->pixelSize.height==800,"epub_layout_render_dimensions");
  require(engine.renderPage(static_cast<int>(count-1),1,false).has_value(),"epub_last_spine_page_renders");
  require(!engine.renderPage(static_cast<int>(count),1,true),"epub_invalid_page_rejected");
  require(engine.openDocument(dir/"image.epub"),"epub_reopen");
  auto reopened=engine.renderPage(0,1,true);
  require(engine.locationCount()==count && engine.layoutIdentity()==identity &&
          reopened && reopened->rgba==first->rgba,"epub_reopen_same_layout_stable");
  auto dark=[](const RenderedRegion& r){std::size_t n=0;for(std::size_t i=0;i<r.rgba.size();i+=4)if(r.rgba[i]<64)++n;return n;};
  require(engine.openDocument(dir/"text.epub"),"epub_text_variant_open");
  auto without=engine.renderPage(0,1,true);
  require(without && dark(*first)>dark(*without)+2000,"epub_embedded_image_renders");
  engine.closeDocument();require(engine.setEpubLayout({600,400,18}),"epub_new_layout_after_close");
  require(engine.openDocument(dir/"image.epub") && engine.locationCount()>count &&
          engine.layoutIdentity()!=identity,"epub_reflow_changes_pages_and_identity");
  engine.closeDocument();
  require(engine.layoutIdentity().empty(),"epub_close_clears_layout_identity");
}
#endif

int main(int argc, char** argv) {
  MuPdfDocumentEngine engine;
  for (auto index : {std::numeric_limits<std::int64_t>::min(), std::int64_t{-1},
                    std::int64_t{0}, std::numeric_limits<std::int64_t>::max()}) {
    DocumentLocation location{index, 0, {}, 0};
    require(!engine.previousLocation(location) && !engine.nextLocation(location),
            "closed_navigation_rejects_invalid_source");
  }
  require(*fitPage({0,0,600,800}, 600,600) == .75 &&
          *fitPage({0,0,800,600}, 600,600) == .75, "portrait_landscape_fit");
  require(!fitPage({0,0,0,800}, 600,600) &&
          !fitPage({0,0,std::numeric_limits<double>::infinity(),800},600,600),
          "invalid_geometry_rejected");
#if defined(INKBOUND_HAS_MUPDF)
  if (argc != 2) return 2;
  require(engine.openDocument(argv[1]), "pdf_open");
  require(engine.locationCount() > 0, "pdf_locations");
  for (auto index : {std::numeric_limits<std::int64_t>::min(), std::int64_t{-1},
                    engine.locationCount(), std::numeric_limits<std::int64_t>::max()}) {
    DocumentLocation location{index, 0, {}, 0};
    require(!engine.previousLocation(location) && !engine.nextLocation(location),
            "open_navigation_rejects_invalid_source");
  }
  auto first = engine.resolveLocation(0);
  auto last = engine.resolveLocation(engine.locationCount()-1);
  require(first && last && !engine.previousLocation(*first) &&
          !engine.nextLocation(*last), "pdf_navigation_boundaries");
  if (engine.locationCount() > 1) {
    auto next=engine.nextLocation(*first), previous=engine.previousLocation(*last);
    require(next && previous && next->index == 1 &&
            previous->index == engine.locationCount()-2,"pdf_navigation_valid_steps");
  }
  epubRegressions(engine);
#else
  (void)argc; (void)argv;
  std::cout << "MuPDF backend tests NOT RUN (backend unavailable)\n";
#endif
}
