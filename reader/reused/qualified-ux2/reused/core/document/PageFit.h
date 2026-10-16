#pragma once
#include "core/document/DocumentTypes.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace inkbound::document {
inline std::optional<double> fitPage(RectF bounds, int width, int height) {
  if (width < 1 || height < 1 || width > 4096 || height > 4096 ||
      !std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
      !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
      bounds.width <= 0 || bounds.height <= 0) return std::nullopt;
  const double scale = std::min(width / bounds.width, height / bounds.height);
  if (!std::isfinite(scale) || scale <= 0 || scale > 16) return std::nullopt;
  return scale;
}

// No caller-supplied filename. Only the packaged development fixture is exposed.
inline std::optional<std::filesystem::path> packagedPdf(
    const std::filesystem::path& applicationRoot) {
  std::error_code error;
  const auto root = std::filesystem::canonical(applicationRoot, error);
  if (error) return std::nullopt;
  const auto pdf = std::filesystem::canonical(
      root / "fixtures/inkbound-device-test.pdf", error);
  if (error) return std::nullopt;
  auto mismatch = std::mismatch(root.begin(), root.end(), pdf.begin(), pdf.end());
  if (mismatch.first != root.end() || mismatch.second == pdf.end() ||
      !std::filesystem::is_regular_file(pdf, error) || error) return std::nullopt;
  const auto size = std::filesystem::file_size(pdf, error);
  if (error || size == 0 || size > 1024 * 1024) return std::nullopt;
  return pdf;
}
}  // namespace inkbound::document
