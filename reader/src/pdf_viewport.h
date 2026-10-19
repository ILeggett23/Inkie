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
// The tablet's turn (docs/LANDSCAPE.md), a presentation parameter never stored in the view: 0 portrait, 90 its top edge
// turned clockwise, 270 anticlockwise. In landscape the viewport is the landscape page area on the panel (1520x2160 at
// x 0 for 90, x 100 for 270) and the content gets the quarter turn that keeps it upright for the reader. The stored pan
// is in the reader's frame: it is turned with the screen and clamped to the landscape range.
inline constexpr unsigned Portrait=0,Clockwise=90,Anticlockwise=270;
struct Viewport {int x{},y{},width{Width},height{Height};};
Viewport viewport(unsigned orientation)noexcept;
struct Plan {
 Rect sourceBounds,crop,visibleSource;View view;Affine forward,inverse;
 double scale{},maxPanX{},maxPanY{};int width{Width},height{Height};std::size_t rasterBytes{RasterBytes};
 int x{},y{};unsigned orientation{}; // the viewport's place on the panel; forward maps source to panel pixels
};
Plan plan(Rect immutableSourceBounds,const View&,unsigned orientation=Portrait);
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
// The raster is the plan's viewport (geometry.width x height), to be placed at (geometry.x, geometry.y) on the panel.
RenderedViewport renderPdfRegion(const std::filesystem::path&,int pageIndex,Rect immutableSourceBounds,const View&,std::string_view expectedSourceSha256={},unsigned orientation=Portrait);
// The page's own bounds (fz_bound_page), for a page without a stored view that is shown turned.
Rect pageBounds(const std::filesystem::path&,int pageIndex,std::string_view expectedSourceSha256={});
}
