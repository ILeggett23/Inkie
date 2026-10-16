#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace inkbound::document {

struct SizeI {
  std::int32_t width{};
  std::int32_t height{};
};

struct PointF {
  double x{};
  double y{};
};

struct RectF {
  double x{};
  double y{};
  double width{};
  double height{};
};

struct DocumentMetadata {
  std::string title;
  std::string author;
  std::string format;
  std::string contentHash;
  bool hasTextLayer{false};
};

struct DocumentLocation {
  std::int64_t index{};
  std::int32_t pageIndex{};
  std::string structuralPath;
  double normalizedOffset{};
};

struct TocEntry {
  std::string title;
  DocumentLocation location;
  std::int32_t depth{};
};

struct RenderRequest {
  DocumentLocation location;
  RectF documentRegion;
  double scale{1.0};
  std::uint32_t backgroundRgba{0xffffffffU};
  bool grayscale{false};
};

struct RenderedRegion {
  SizeI pixelSize;
  std::int32_t strideBytes{};
  RectF renderedDocumentRegion;
  std::vector<std::uint8_t> rgba;
};

struct TextPosition {
  DocumentLocation location;
  std::int64_t characterOffset{};
  RectF glyphBounds;
};

struct TextSelection {
  TextPosition start;
  TextPosition end;
  std::string text;
  std::vector<RectF> quads;
};

struct SearchResult {
  DocumentLocation location;
  std::string excerpt;
  std::vector<RectF> quads;
};

}  // namespace inkbound::document

