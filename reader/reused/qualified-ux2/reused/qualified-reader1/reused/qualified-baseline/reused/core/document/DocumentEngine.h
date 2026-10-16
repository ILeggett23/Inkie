#pragma once

#include "core/document/DocumentAnchor.hpp"
#include "core/document/DocumentTypes.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace inkbound::document {

// Platform-neutral boundary around all document parsing, layout, text, and
// rendering. Implementations may use MuPDF, a commercially licensed engine, or
// a test double without leaking engine-owned objects into the application.
class DocumentEngine {
 public:
  virtual ~DocumentEngine() = default;

  virtual bool openDocument(const std::filesystem::path& path) = 0;
  virtual void closeDocument() noexcept = 0;
  [[nodiscard]] virtual bool isOpen() const noexcept = 0;
  [[nodiscard]] virtual std::string lastError() const = 0;

  [[nodiscard]] virtual DocumentMetadata documentMetadata() const = 0;
  [[nodiscard]] virtual std::vector<TocEntry> tableOfContents() const = 0;
  [[nodiscard]] virtual std::int64_t locationCount() const = 0;
  [[nodiscard]] virtual std::optional<DocumentLocation> resolveLocation(
      std::int64_t logicalIndex) const = 0;

  [[nodiscard]] virtual std::optional<RenderedRegion> renderRegion(
      const RenderRequest& request) = 0;
  [[nodiscard]] virtual std::optional<RenderedRegion> renderPage(
      std::int32_t pageIndex, double scale, bool grayscale) = 0;
  // Optional fixed-page geometry; unavailable engines need not implement it.
  [[nodiscard]] virtual std::optional<RectF> pageBounds(std::int32_t) {
    return std::nullopt;
  }

  [[nodiscard]] virtual std::string extractText(
      const DocumentLocation& location,
      std::optional<RectF> region = std::nullopt) = 0;
  [[nodiscard]] virtual std::optional<TextPosition> hitTestText(
      const DocumentLocation& location, PointF point) = 0;
  [[nodiscard]] virtual std::optional<TextSelection> selectWord(
      const TextPosition& position) = 0;
  [[nodiscard]] virtual std::optional<TextSelection> expandSelection(
      const TextSelection& selection, TextPosition newExtent) = 0;
  [[nodiscard]] virtual std::vector<RectF> selectionQuads(
      const TextSelection& selection) const = 0;
  [[nodiscard]] virtual std::vector<SearchResult> searchText(
      std::string_view query, std::size_t resultLimit) = 0;

  [[nodiscard]] virtual std::optional<DocumentAnchor> createStableAnchor(
      const TextSelection& selection) const = 0;
  [[nodiscard]] virtual std::optional<DocumentLocation> resolveStableAnchor(
      const DocumentAnchor& anchor) const = 0;

  [[nodiscard]] virtual std::optional<DocumentLocation> previousLocation(
      const DocumentLocation& location) const = 0;
  [[nodiscard]] virtual std::optional<DocumentLocation> nextLocation(
      const DocumentLocation& location) const = 0;
};

using DocumentEnginePtr = std::unique_ptr<DocumentEngine>;

}  // namespace inkbound::document
