#pragma once

#include "core/document/DocumentEngine.h"

#include <memory>

namespace inkbound::document {

// MuPDF adapter. The implementation compiles to an explicit unavailable stub
// unless INKBOUND_HAS_MUPDF is enabled by a verified build configuration.
class MuPdfDocumentEngine final : public DocumentEngine {
 public:
  struct EpubLayout {
    int width{600};
    int height{800};
    int fontSize{12};
  };
  // Configure before opening. A layout stays immutable while a document is open.
  bool setEpubLayout(EpubLayout layout);
  [[nodiscard]] std::string layoutIdentity() const;

  MuPdfDocumentEngine();
  ~MuPdfDocumentEngine() override;

  MuPdfDocumentEngine(const MuPdfDocumentEngine&) = delete;
  MuPdfDocumentEngine& operator=(const MuPdfDocumentEngine&) = delete;
  MuPdfDocumentEngine(MuPdfDocumentEngine&&) noexcept;
  MuPdfDocumentEngine& operator=(MuPdfDocumentEngine&&) noexcept;

  bool openDocument(const std::filesystem::path& path) override;
  void closeDocument() noexcept override;
  [[nodiscard]] bool isOpen() const noexcept override;
  [[nodiscard]] std::string lastError() const override;
  [[nodiscard]] DocumentMetadata documentMetadata() const override;
  [[nodiscard]] std::vector<TocEntry> tableOfContents() const override;
  [[nodiscard]] std::int64_t locationCount() const override;
  [[nodiscard]] std::optional<DocumentLocation> resolveLocation(
      std::int64_t logicalIndex) const override;
  [[nodiscard]] std::optional<RenderedRegion> renderRegion(
      const RenderRequest& request) override;
  [[nodiscard]] std::optional<RenderedRegion> renderPage(
      std::int32_t pageIndex, double scale, bool grayscale) override;
  [[nodiscard]] std::optional<RectF> pageBounds(std::int32_t pageIndex) override;
  [[nodiscard]] std::string extractText(
      const DocumentLocation& location,
      std::optional<RectF> region = std::nullopt) override;
  [[nodiscard]] std::optional<TextPosition> hitTestText(
      const DocumentLocation& location, PointF point) override;
  [[nodiscard]] std::optional<TextSelection> selectWord(
      const TextPosition& position) override;
  [[nodiscard]] std::optional<TextSelection> expandSelection(
      const TextSelection& selection, TextPosition newExtent) override;
  [[nodiscard]] std::vector<RectF> selectionQuads(
      const TextSelection& selection) const override;
  [[nodiscard]] std::vector<SearchResult> searchText(
      std::string_view query, std::size_t resultLimit) override;
  [[nodiscard]] std::optional<DocumentAnchor> createStableAnchor(
      const TextSelection& selection) const override;
  [[nodiscard]] std::optional<DocumentLocation> resolveStableAnchor(
      const DocumentAnchor& anchor) const override;
  [[nodiscard]] std::optional<DocumentLocation> previousLocation(
      const DocumentLocation& location) const override;
  [[nodiscard]] std::optional<DocumentLocation> nextLocation(
      const DocumentLocation& location) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace inkbound::document
