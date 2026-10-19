#pragma once
#include "epub_reflow.h"
#include "epub_view_store.h"
#include "underlay.h"
namespace reader {
struct StagedEpubView {QString viewId;int targetPage{};QJsonObject readingAnchor;};
// Helper-only: source-anchored target selection and immutable receipt staging.
// Does not activate a view or update resume state.
StagedEpubView stageEpubView(const std::filesystem::path& application,const std::filesystem::path& documentRoot,const QString& currentViewId,epub::Layout requested,int currentPage);
// The exact baseline delegates to the unchanged import preparation path.
// Derived geometry/raster share one immutable ReflowDocument/layout instance.
// orientation (docs/LANDSCAPE.md): 90/270 renders a landscape layout's page turned into epubviews::turnedStoragePath.
underlay::Prepared prepareEpubView(const std::filesystem::path& application,const std::filesystem::path& documentRoot,int page,const epubviews::ViewSet& immutable,unsigned orientation=0);
}
