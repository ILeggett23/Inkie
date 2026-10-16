#pragma once
#include "document_view_store.h"
#include "underlay.h"
namespace reader {
// Helper-only. The immutable set is descriptor-bound and must already be staged.
// Baseline entries delegate to the unchanged qualified preparation path.
underlay::Prepared preparePdfView(const std::filesystem::path& application,const std::filesystem::path& documentRoot,int sourcePage,const viewstore::ViewSet& immutable);
}
