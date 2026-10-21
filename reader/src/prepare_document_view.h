#pragma once
#include "document_view_store.h"
#include "underlay.h"
namespace reader {
// Helper-only. The immutable set is descriptor-bound and must already be staged.
// Baseline entries delegate to the unchanged qualified preparation path.
// orientation (docs/LANDSCAPE.md): 0 portrait (unchanged); 90 or 270 renders the page turned for landscape into
// viewstore::turnedStoragePath, with the stored view or the default one, and no baked footer (the shell paints it).
underlay::Prepared preparePdfView(const std::filesystem::path& application,const std::filesystem::path& documentRoot,int sourcePage,const viewstore::ViewSet& immutable,unsigned orientation=0);
}
