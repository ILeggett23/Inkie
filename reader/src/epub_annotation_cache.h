#pragma once
#include "epub_view_store.h"
#include "semantic.h"
namespace reader::epubannotations {
void saveCache(const std::filesystem::path&,const epubviews::ViewSet&,const QJsonObject&);
// True when this layout's cache was derived from exactly these annotations (annotationsSha256, written by the stage
// command): staging the layout again (turning back and forth) need not derive it again.
bool cacheCurrent(const std::filesystem::path&,const epubviews::ViewSet&,const QString& annotationsSha256);
semantic::Geometry projectCache(const std::filesystem::path&,const epubviews::ViewSet&,const semantic::Geometry&,const QByteArray& binding);
}
