#pragma once
#include "epub_typography.h"
#include <QJsonObject>
#include <QString>
#include <atomic>
#include <filesystem>
namespace reader {
struct EpubStageResult {QString viewId;int targetPage{};QJsonObject readingAnchor;};
// Off the input thread. The parser runs only in the isolated helper; this call
// stages a verified immutable receipt and never activates it.
EpubStageResult stageEpubViewViaHelper(const std::filesystem::path& application,const std::filesystem::path& documentRoot,const QString& currentView,int width,int font,int currentPage,const std::atomic<bool>& stop,epub::Typography typography={});
}
