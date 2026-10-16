#pragma once
#include "epub_view_store.h"
#include "semantic.h"
namespace reader::epubannotations {
void saveCache(const std::filesystem::path&,const epubviews::ViewSet&,const QJsonObject&);
semantic::Geometry projectCache(const std::filesystem::path&,const epubviews::ViewSet&,const semantic::Geometry&,const QByteArray& binding);
}
