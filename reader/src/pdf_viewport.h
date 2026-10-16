#pragma once
#include "core/document/DocumentTypes.h"
#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
namespace reader::pdfview {
using Rect=inkbound::document::RectF;
using Point=inkbound::document::PointF;
inline constexpr int Width=1620,Height=2060;
inline constexpr std::size_t RasterBytes=std::size_t(Width)*Height*4;
inline constexpr std::size_t MaxSourceBytes=64*1024*1024;
inline constexpr double MaxScale=64,MaxCoordinate=100000,MaxAffineTranslation=1000000;
enum class Error {InvalidBounds,InvalidView,CropOutsideSource,PanOutsideRange,Capacity,SourceUnavailable,SourceMismatch,InvalidPage,BackendUnavailable,RenderFailed};
class Failure final:public std::runtime_error {
 Error error_;
public:Failure(Error error,const char* message):std::runtime_error(message),error_(error){}Error code()const noexcept{return error_;}
};
struct Affine {
 // x'=a*x+c*y+e, y'=b*x+d*y+f; source coordinates retain their nonzero origin.
 std::array<double,6> value{1,0,0,1,0,0};
 Point map(Point)const noexcept;
};
struct View {
 double zoom{1};unsigned rotation{};std::optional<Rect> crop;
 // Pixel translation of the rotated content, after zoom. Positive is right/down.
 // Pan is rejected beyond the limit that keeps the viewport over the crop.
 Point pan{};
};
struct Plan {
 Rect sourceBounds,crop,visibleSource;View view;Affine forward,inverse;
 double scale{},maxPanX{},maxPanY{};int width{Width},height{Height};std::size_t rasterBytes{RasterBytes};
};
Plan plan(Rect immutableSourceBounds,const View&);
struct RenderedViewport {
 Plan geometry;
 // Fixed viewport RGBA; visibleSource describes its source intersection, while
 // geometry.forward/inverse is authoritative for rotation and white margins.
 inkbound::document::RenderedRegion raster;
};
// Link pdf_viewport.cpp once as the pure planning library. This function is
// defined only by pdf_viewport_backend.cpp in the helper backend library; no stub.
// Helper thread/process only. Uses a separate clipped MuPDF adapter; the reused
// DocumentEngine::renderRegion fallback is deliberately not changed or called.
// Optional expected SHA256 verifies the exact immutable bytes before parsing.
// Caller owns descriptor identity verification and helper CPU/wall limits.
// This adapter reads one bounded regular source file and checks actual bounds.
RenderedViewport renderPdfRegion(const std::filesystem::path&,int pageIndex,Rect immutableSourceBounds,const View&,std::string_view expectedSourceSha256={});
}
