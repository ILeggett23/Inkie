#pragma once
#include <QJsonObject>
#include <QString>
#include <filesystem>
namespace reader::epubviews {
inline constexpr unsigned MaxSets=256;
inline constexpr std::size_t MaxMetadataBytes=16*1024*1024;
struct ViewSet {QString id;QJsonObject receipt;};
// Empty ID is immutable import baseline (default 600x800, font 12). For stage,
// layoutId is always ReflowDocument's canonical epub-reflow-v1:SHA(layout).
ViewSet readSet(const std::filesystem::path& root,const QString& id);
ViewSet activeSet(const std::filesystem::path& root);
QString stage(const std::filesystem::path& root,const QJsonObject& layoutDescriptor,const QString& layoutId,int pages);
void activate(const std::filesystem::path& root,const QString& id,const QString& expectedOld);
std::filesystem::path storagePath(const std::filesystem::path& root,const ViewSet&,int page);
// A landscape layout's page shown turned (docs/LANDSCAPE.md): <document>/turn-<90|270>/epub-views/<id>/page-N, inspected
// and reclaimed like the document's own derived sets. Orientation 0 is storagePath.
std::filesystem::path turnedStoragePath(const std::filesystem::path& root,const ViewSet&,int page,unsigned orientation);
}
