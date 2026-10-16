#include "core/document/MuPdfDocumentEngine.h"

#include "core/document/DocumentAnchor.hpp"
#include "core/document/PageFit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

#if defined(INKBOUND_HAS_MUPDF)
extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}
#endif

namespace inkbound::document {

struct MuPdfDocumentEngine::Impl {
  mutable std::mutex mutex;
  std::string error;
  DocumentMetadata metadata;
  std::filesystem::path path;
#if defined(INKBOUND_HAS_MUPDF)
  fz_context* context{nullptr};
  fz_document* document{nullptr};
#endif
  std::int32_t pageCount{};
};

MuPdfDocumentEngine::MuPdfDocumentEngine() : impl_(std::make_unique<Impl>()) {}

MuPdfDocumentEngine::~MuPdfDocumentEngine() { closeDocument(); }

MuPdfDocumentEngine::MuPdfDocumentEngine(MuPdfDocumentEngine&&) noexcept =
    default;
MuPdfDocumentEngine& MuPdfDocumentEngine::operator=(
    MuPdfDocumentEngine&& other) noexcept {
  if (this != &other) {
    closeDocument();
    impl_ = std::move(other.impl_);
  }
  return *this;
}

bool MuPdfDocumentEngine::openDocument(const std::filesystem::path& path) {
  closeDocument();
  std::scoped_lock lock(impl_->mutex);
  impl_->error.clear();

  std::error_code fileError;
  if (path.empty() || !std::filesystem::is_regular_file(path, fileError) || fileError) {
    impl_->error = "Document path is not a readable regular file";
    return false;
  }

#if !defined(INKBOUND_HAS_MUPDF)
  (void)path;
  impl_->error =
      "This build has no MuPDF backend; configure with a verified MuPDF Core "
      "installation and INKBOUND_ENABLE_MUPDF=ON";
  return false;
#else
  impl_->context = fz_new_context(nullptr, nullptr, 32 * 1024 * 1024);
  if (impl_->context == nullptr) {
    impl_->error = "MuPDF failed to allocate its context";
    return false;
  }

  // Suppress upstream diagnostics that can contain untrusted document strings.
  fz_set_error_callback(impl_->context, [](void*, const char*) {}, nullptr);
  fz_set_warning_callback(impl_->context, [](void*, const char*) {}, nullptr);
  const auto encodedPath = path.u8string();
  fz_try(impl_->context) {
    fz_register_document_handlers(impl_->context);
    impl_->document = fz_open_document(
        impl_->context, reinterpret_cast<const char*>(encodedPath.c_str()));
    pdf_document* pdf = pdf_specifics(impl_->context, impl_->document);
    if (pdf == nullptr) fz_throw(impl_->context, FZ_ERROR_FORMAT, "PDF required");
    if (pdf->crypt != nullptr || fz_needs_password(impl_->context, impl_->document))
      fz_throw(impl_->context, FZ_ERROR_ARGUMENT, "Encrypted PDF unsupported");
    impl_->pageCount = fz_count_pages(impl_->context, impl_->document);
  }
  fz_catch(impl_->context) {
    impl_->error = fz_caught(impl_->context) == FZ_ERROR_ARGUMENT
        ? "encrypted-pdf-unsupported" : "pdf-open-failed";
  }

  if (!impl_->error.empty() || impl_->document == nullptr || impl_->pageCount <= 0 || impl_->pageCount > 1000) {
    if (impl_->error.empty()) {
      impl_->error = "MuPDF opened no renderable pages";
    }
    if (impl_->document != nullptr) {
      fz_drop_document(impl_->context, impl_->document);
      impl_->document = nullptr;
    }
    fz_drop_context(impl_->context);
    impl_->context = nullptr;
    impl_->pageCount = 0;
    return false;
  }

  impl_->path = path;
  impl_->metadata.title = path.stem().string();
  impl_->metadata.format = "PDF";
  // Text-layer detection is deliberately deferred until structured-text
  // extraction is implemented and verified across the selected MuPDF version.
  impl_->metadata.hasTextLayer = false;
  return true;
#endif
}

void MuPdfDocumentEngine::closeDocument() noexcept {
  if (!impl_) {
    return;
  }
  std::scoped_lock lock(impl_->mutex);
#if defined(INKBOUND_HAS_MUPDF)
  if (impl_->document != nullptr) {
    fz_drop_document(impl_->context, impl_->document);
    impl_->document = nullptr;
  }
  if (impl_->context != nullptr) {
    fz_drop_context(impl_->context);
    impl_->context = nullptr;
  }
#endif
  impl_->path.clear();
  impl_->metadata = {};
  impl_->pageCount = 0;
}

bool MuPdfDocumentEngine::isOpen() const noexcept {
  std::scoped_lock lock(impl_->mutex);
#if defined(INKBOUND_HAS_MUPDF)
  return impl_->document != nullptr;
#else
  return false;
#endif
}

std::string MuPdfDocumentEngine::lastError() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->error;
}

DocumentMetadata MuPdfDocumentEngine::documentMetadata() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->metadata;
}

std::vector<TocEntry> MuPdfDocumentEngine::tableOfContents() const { return {}; }

std::int64_t MuPdfDocumentEngine::locationCount() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->pageCount;
}

std::optional<DocumentLocation> MuPdfDocumentEngine::resolveLocation(
    std::int64_t logicalIndex) const {
  std::scoped_lock lock(impl_->mutex);
  if (logicalIndex < 0 || logicalIndex >= impl_->pageCount) {
    return std::nullopt;
  }
  return DocumentLocation{logicalIndex, static_cast<std::int32_t>(logicalIndex),
                          {}, 0.0};
}

std::optional<RenderedRegion> MuPdfDocumentEngine::renderPage(
    std::int32_t pageIndex, double scale, bool grayscale) {
  if (!std::isfinite(scale) || scale <= 0.0 || scale > 16.0) {
    std::scoped_lock lock(impl_->mutex);
    impl_->error = "Render scale must be finite and within (0, 16]";
    return std::nullopt;
  }

  std::scoped_lock lock(impl_->mutex);
#if !defined(INKBOUND_HAS_MUPDF)
  (void)pageIndex;
  (void)grayscale;
  impl_->error = "MuPDF rendering is unavailable in this build";
  return std::nullopt;
#else
  if (impl_->document == nullptr || pageIndex < 0 ||
      pageIndex >= impl_->pageCount) {
    impl_->error = "Page index is outside the open document";
    return std::nullopt;
  }

  fz_page* page = nullptr;
  fz_pixmap* pixmap = nullptr;
  fz_var(page);
  fz_var(pixmap);
  // Only POD values and MuPDF operations occur in this longjmp region.
  fz_try(impl_->context) {
    page = fz_load_page(impl_->context, impl_->document, pageIndex);
    const fz_rect bounds = fz_bound_page(impl_->context, page);
    const double w = static_cast<double>(bounds.x1) - bounds.x0;
    const double h = static_cast<double>(bounds.y1) - bounds.y0;
    if (!std::isfinite(bounds.x0) || !std::isfinite(bounds.y0) ||
        !std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0 ||
        w * scale > 4094 || h * scale > 4094 ||
        std::abs(static_cast<double>(bounds.x0) * scale) > 1000000 ||
        std::abs(static_cast<double>(bounds.y0) * scale) > 1000000)
      fz_throw(impl_->context, FZ_ERROR_LIMIT, "Invalid page geometry");
    const fz_matrix transform = fz_scale(static_cast<float>(scale),
                                         static_cast<float>(scale));
    pixmap = fz_new_pixmap_from_page(impl_->context, page, transform,
                                     grayscale ? fz_device_gray(impl_->context)
                                               : fz_device_rgb(impl_->context),
                                     0);  // Opaque white, never transparent black.
  }
  fz_always(impl_->context) {
    fz_drop_page(impl_->context, page);
  }
  fz_catch(impl_->context) {
    fz_drop_pixmap(impl_->context, pixmap);
    impl_->error = "page-render-failed";
    return std::nullopt;
  }
  auto drop = [this](fz_pixmap* value) { fz_drop_pixmap(impl_->context, value); };
  std::unique_ptr<fz_pixmap, decltype(drop)> owned(pixmap, drop);
  const int width = pixmap->w, height = pixmap->h;
  const int components = pixmap->n;
  const auto stride = pixmap->stride;
  const auto* samples = pixmap->samples;
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
      components != (grayscale ? 1 : 3) || pixmap->alpha != 0 ||
      samples == nullptr || stride < width * components ||
      stride > 4096 * 4 || static_cast<std::size_t>(height) *
          static_cast<std::size_t>(stride) > 64U * 1024U * 1024U) {
    impl_->error = "invalid-pixel-buffer";
    return std::nullopt;
  }
  RenderedRegion rendered;
  rendered.pixelSize = {width, height};
  rendered.strideBytes = width * 4;
  rendered.renderedDocumentRegion = {pixmap->x / scale, pixmap->y / scale,
                                    width / scale, height / scale};
  try {
    rendered.rgba.resize(static_cast<std::size_t>(rendered.strideBytes) *
                         static_cast<std::size_t>(height));
  } catch (const std::bad_alloc&) {
    impl_->error = "pixel-allocation-failed";
    return std::nullopt;
  }
  for (int y = 0; y < height; ++y) {
    const auto* row = samples + static_cast<std::ptrdiff_t>(y) * stride;
    auto* dest = rendered.rgba.data() + static_cast<std::ptrdiff_t>(y) * rendered.strideBytes;
    for (int x = 0; x < width; ++x) {
      const auto* pixel = row + x * components;
      dest[x * 4] = pixel[0];
      dest[x * 4 + 1] = pixel[grayscale ? 0 : 1];
      dest[x * 4 + 2] = pixel[grayscale ? 0 : 2];
      dest[x * 4 + 3] = 255;
    }
  }
  return rendered;
#endif
}

std::optional<RectF> MuPdfDocumentEngine::pageBounds(std::int32_t index) {
  std::scoped_lock lock(impl_->mutex);
#if !defined(INKBOUND_HAS_MUPDF)
  (void)index;
  return std::nullopt;
#else
  if (!impl_->document || index < 0 || index >= impl_->pageCount) return std::nullopt;
  fz_page* page = nullptr;
  fz_rect bounds = fz_empty_rect;
  fz_var(page);
  fz_var(bounds);
  fz_try(impl_->context) {
    page = fz_load_page(impl_->context, impl_->document, index);
    bounds = fz_bound_page(impl_->context, page);
  }
  fz_always(impl_->context) { fz_drop_page(impl_->context, page); }
  fz_catch(impl_->context) {
    impl_->error = "page-load-failed";
    return std::nullopt;
  }
  RectF result{bounds.x0, bounds.y0, static_cast<double>(bounds.x1) - bounds.x0,
               static_cast<double>(bounds.y1) - bounds.y0};
  if (!fitPage(result, 1, 1)) return std::nullopt;
  return result;
#endif
}

std::optional<RenderedRegion> MuPdfDocumentEngine::renderRegion(
    const RenderRequest& request) {
  // The first slice renders a full page. Region/tile clipping will replace this
  // deliberately correct fallback once MuPDF display-list tiling is profiled.
  return renderPage(request.location.pageIndex, request.scale,
                    request.grayscale);
}

std::string MuPdfDocumentEngine::extractText(
    const DocumentLocation&, std::optional<RectF>) {
  std::scoped_lock lock(impl_->mutex);
  impl_->error = "Structured-text extraction is not implemented in this slice";
  return {};
}

std::optional<TextPosition> MuPdfDocumentEngine::hitTestText(
    const DocumentLocation&, PointF) {
  return std::nullopt;
}

std::optional<TextSelection> MuPdfDocumentEngine::selectWord(
    const TextPosition&) {
  return std::nullopt;
}

std::optional<TextSelection> MuPdfDocumentEngine::expandSelection(
    const TextSelection&, TextPosition) {
  return std::nullopt;
}

std::vector<RectF> MuPdfDocumentEngine::selectionQuads(
    const TextSelection& selection) const {
  return selection.quads;
}

std::vector<SearchResult> MuPdfDocumentEngine::searchText(std::string_view,
                                                          std::size_t) {
  return {};
}

std::optional<DocumentAnchor> MuPdfDocumentEngine::createStableAnchor(
    const TextSelection&) const {
  return std::nullopt;
}

std::optional<DocumentLocation> MuPdfDocumentEngine::resolveStableAnchor(
    const DocumentAnchor&) const {
  return std::nullopt;
}

std::optional<DocumentLocation> MuPdfDocumentEngine::previousLocation(
    const DocumentLocation& location) const {
  return resolveLocation(location.index - 1);
}

std::optional<DocumentLocation> MuPdfDocumentEngine::nextLocation(
    const DocumentLocation& location) const {
  return resolveLocation(location.index + 1);
}

}  // namespace inkbound::document
