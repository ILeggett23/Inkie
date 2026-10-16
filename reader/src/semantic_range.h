#pragma once
#include "semantic.h"
namespace semantic {
inline constexpr unsigned MaxScreenRangeGlyphs=4096,MaxScreenRangeUtf16=16000;
enum class RangeStatus {Ready,NoTextLayer,Unavailable,InvalidCoordinates,InvalidGeometry,StartMiss,EndMiss,CapacityExceeded};
struct ScreenRangeResult {RangeStatus status{RangeStatus::Unavailable};Selection selection;unsigned glyphCount{},utf16Count{};bool replace{};};
// Finger-release / intercontact only. Without walkIn both endpoints must hit actual token glyphs. With walkIn (pen
// selection, highlighter snapping) an end that lands just beside the text walks in along the drag to the first point
// on a word, so a stroke may start in the margin or between lines. Selection always snaps to whole words.
// Caller rejects a changed page/view epoch before passing the immutable geometry.
// Source punctuation and line breaks are retained; no dehyphenation.
ScreenRangeResult selectScreenRangeChecked(const Geometry&,double startX,double startY,double endX,double endY,std::uint64_t generation,bool walkIn=false,bool blocks=false);
Selection selectScreenRange(const Geometry&,double startX,double startY,double endX,double endY,std::uint64_t generation);
const char* rangeStatusText(RangeStatus)noexcept;
}
namespace semantic {
inline constexpr unsigned MaxSelectionRanges=32;
bool validAnchorSegments(const QJsonObject&);
QJsonArray selectionRanges(const QJsonObject&);
Selection selectionFromRanges(const Geometry&,const QJsonArray&,std::uint64_t generation);
Selection mergeSelection(const Geometry&,Selection previous,Selection incoming,std::uint64_t generation,bool append=true);
Selection replaceSelectionEdge(const Geometry&,Selection current,Selection incoming,bool first,std::uint64_t generation);
}
