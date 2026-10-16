#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <vector>
#include <cstdint>
#include <QString>
#include <filesystem>
#include <memory>
namespace epub {
// userCss: only a stylesheet from epub_typography.h (empty = the book's own typography).
struct Layout {int width{600},height{800},fontSize{12};QString userCss{};};
struct PageRaster {std::vector<std::uint32_t> rgb32;QJsonArray bounds,forward,inverse;int width{1620},height{2060},stride{1620};};
// Helper-only; one owner thread. Input must be an already validated library EPUB.
// Its expected source hash is checked before parsing an immutable memory snapshot.
// No opaque MuPDF bookmarks or descriptor changes are produced.
class ReflowDocument {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 ReflowDocument(const std::filesystem::path&,QString documentId,QString expectedSourceSha256,Layout={});
 ~ReflowDocument();ReflowDocument(const ReflowDocument&)=delete;ReflowDocument& operator=(const ReflowDocument&)=delete;
 int pageCount()const;QString sourceSha256()const;QString layoutId()const;
 QJsonObject layoutDescriptor()const;
 PageRaster renderPage(int globalPage); // Same immutable document/layout as text and anchors.
 QJsonObject page(int globalPage);
 // Capture returns {status:"anchored",anchor:{...}} or explicit unresolved reason.
 QJsonObject captureSelection(int globalPage,int firstUtf16,int lastUtf16,const QString& expectedText);
 QJsonObject captureReadingPosition(int globalPage);
 QJsonObject captureSpineStart(int globalPage); // Textless first chapter/page only; verified OPF identity.
 // Approximate reading position for a layout change whose text anchor cannot be resolved exactly (line breaks and
 // hyphenation differ between font sizes): the page's chapter and how far into that chapter's text the page starts.
 struct ChapterFraction {int chapter{};double fraction{};};
 ChapterFraction chapterFraction(int globalPage);
 int pageAtFraction(int chapter,double fraction); // the page of `chapter` holding that share of its text
 // Resolve returns actual page fragments/quads, or unresolved; never page guessing.
 QJsonObject resolve(const QJsonObject& anchor);
};
}
