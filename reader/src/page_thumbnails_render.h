// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "page_thumbnails.h"
namespace thumbs {
// Helper process only (MuPDF). Renders pages [first,first+count) of a library document in the given view: the PDF's
// original pages, or the EPUB layout recorded in that view's receipt (verified against the reflowed document).
std::vector<Image> renderBatch(const std::filesystem::path& documentData,const QString& view,int first,int count);
}
