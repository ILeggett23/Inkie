#pragma once
#include "pdf_viewport.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <optional>
namespace reader::viewstore {
inline constexpr unsigned MaxSets=256;
inline constexpr std::size_t MaxMetadataBytes=16*1024*1024;
struct PdfPage {pdfview::Rect bounds;pdfview::View view;};
struct ViewSet {QString id;QJsonObject receipt;};
// root is the selected document root. Empty set/config IDs mean the preserved
// baseline namespace. Receipts never modify the immutable library descriptor.
ViewSet readSet(const std::filesystem::path& root,const QString& id);
ViewSet activeSet(const std::filesystem::path& root);
QString stagePdf(const std::filesystem::path& root,const QString& previousSetID,int sourcePage,pdfview::Rect bounds,const pdfview::View&);
void activate(const std::filesystem::path& root,const QString& id,const QString& expectedPrevious);
std::optional<PdfPage> pageEntry(const ViewSet&,int sourcePage);
QString pageConfigId(const ViewSet&,int sourcePage);
std::filesystem::path storagePath(const std::filesystem::path& root,const ViewSet&,int sourcePage);
// A page shown turned (docs/LANDSCAPE.md): the orientation is never stored in the set. Its prepared page lives in
// <document>/turn-<orientation>/ beside the portrait tree, renders the page's stored view (or the default view when the
// page has none) and is identified by the page configuration plus the orientation. Orientation 0 is storagePath.
QJsonObject viewSettings(const ViewSet&,int sourcePage); // the stored view, or the default view
QString turnedConfigId(const ViewSet&,int sourcePage,const QJsonArray& bounds,unsigned orientation);
std::filesystem::path turnedStoragePath(const std::filesystem::path& root,const ViewSet&,int sourcePage,unsigned orientation);
}
