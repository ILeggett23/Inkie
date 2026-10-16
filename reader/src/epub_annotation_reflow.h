#pragma once
#include "epub_reflow.h"
#include "epub_view_store.h"
namespace reader::epubannotations {
// Helper-thread only. ViewSet arguments must come from epubviews::readSet.
// All outputs are derived evidence; callers must retain original canonical anchors.
QString anchorDigest(const QJsonObject& original);
QJsonObject capture(epub::ReflowDocument& original, const epubviews::ViewSet&, const QJsonObject& originalAnchor);
QJsonObject resolve(epub::ReflowDocument& target, const epubviews::ViewSet&, const QJsonObject& captureResult);
// Reads immutable source/receipts only. annotationDocument is a locked store snapshot.
// Every entry carries originalAnchorDigest, originalAnchor, status and either actual
// fragments/sourceAnchor or an explicit reason. No guessed target page is emitted.
QJsonObject derive(const std::filesystem::path& documentRoot, const QJsonObject& annotationDocument,
                   const epubviews::ViewSet& target);
}
