#pragma once
#include "library.h"
#include "semantic.h"
namespace product {
QJsonObject featureBinding(const library::Document&);
// Validates active catalogue identity; returns only the library-owned feature directory.
std::filesystem::path featureDirectory(const std::filesystem::path& libraryRoot,const library::Document&);
QJsonObject pageSnapshot(const library::Document&,const underlay::Prepared&);
QJsonObject selectionAnchor(const QJsonObject& page,const QJsonObject& selection);
QJsonObject selectionAnchor(const library::Document&,const underlay::Prepared&,const semantic::SelectionSnapshot&);
}
