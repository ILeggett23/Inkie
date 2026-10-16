#pragma once
#include <filesystem>
#include <string>
#include <QByteArray>
namespace library {
// Helper-only preflight. Caller must subsequently parse/open with the document engine.
// PDF actions are not executed here; this is not a PDF active-content sanitizer.
QByteArray validatedEpubCover(const std::filesystem::path&);
void validateDocumentContainer(const std::filesystem::path& path,const std::string& format);
}
