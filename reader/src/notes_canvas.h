// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "work_tracking.h"
#include "styled_ink.h"
#include "stroke_playback.h"
#include <algorithm>
#include <QRectF>
namespace notes {
struct Window {spike::Rect area{70,750,1550,1790};int scroll{};styled::Style style{};};
// D2 capacity (raised from 256 strokes / 262144 points per note); shared by the canvas and loaders.
inline constexpr unsigned MaxStrokes=2048,MaxTotalPoints=1048576;
// Strokes that may wait for their save while the pen keeps writing (4 -> 16, as styled ink since 19f).
inline constexpr unsigned QueueSlots=16;
class Canvas final:public spike::OperationSink,public tomink::PlaybackTarget {
 static constexpr unsigned Slots=QueueSlots,MaxPoints=2048,MaxTotal=MaxTotalPoints;
 enum State:unsigned {Free,Active,Queued,Failed};
 struct Stroke {std::array<styled::Coordinate,MaxPoints> points{};unsigned count{};bool ignored{},eraser{},hardwareEraser{},started{},interrupted{};styled::Style style{};std::atomic<unsigned> state{Free};recovery::v2::progress::WorkToken tracking;};
 reader::lifecycle::Tracking tracking_;
 std::unique_ptr<std::array<Stroke,Slots>> slots_;spike::Spsc<unsigned,Slots+1> queue_;int active_{-1};Window window_{};styled::Matrix matrix_{};bool eraser_{};
 std::atomic<unsigned> pending_{0},points_{0},strokes_{0};std::atomic<bool> enabled_{false},contact_{false},capacity_{false},failed_{false};
 std::vector<std::uint32_t> scratch_;styled::Raster raster_;std::unique_ptr<styled::Raster> generated_;spike::Rect generatedArea_{};styled::Matrix generatedMatrix_{};bool playbackPrepared_{};
public:
 Canvas(reader::lifecycle::Tracking tracking={});reader::lifecycle::Tracking tracking()const noexcept{return tracking_;}bool reserve()noexcept override;bool append(const spike::Point&)noexcept override;void cancel()noexcept override;
 void preparePlayback(QRectF);bool playBegin(styled::Style s)noexcept{return !contact_&&!pending_&&(playbackPrepared_?(!generated_||generated_->begin(s,generatedMatrix_)):raster_.begin(s,matrix_));}bool playPoint(styled::Coordinate p,bool first)noexcept{return playbackPrepared_?(!generated_||generated_->point(p,first)):raster_.point(p,first);}void playEnd()noexcept{if(playbackPrepared_){if(generated_)generated_->end();}else raster_.end();}void playFill(const QJsonObject& s){if(playbackPrepared_){if(generated_)generated_->fill(s,generatedMatrix_);}else raster_.fill(s,matrix_);}
 void stop()noexcept;void setStyle(styled::Style style)noexcept{window_.style=style;}bool setEraser(bool value)noexcept{if(contact_)return false;eraser_=value;return true;}bool eraser()const noexcept{return eraser_;} /* Main-thread choice; queued slots already own their style. */
 bool take(unsigned&,QJsonObject&,bool& eraser);void acknowledge(unsigned,bool saved)noexcept;
 // A queued stroke that has no note to go to (its note's create failed): freed without failing the canvas, so the
 // sheet reports it and can close instead of ending the session.
 void discard(unsigned)noexcept;
 // D2: points the current stroke piece can still take (0 when no stroke is active). The display
 // closes a piece before it reaches this bound and continues the stroke in a new piece.
 std::size_t room()const noexcept;
 void enable(bool v)noexcept{enabled_=v;}bool contact()const noexcept{return contact_;}unsigned pending()const noexcept{return pending_;}
 bool writable()const noexcept{return enabled_&&!failed_&&!capacity_&&pending_<Slots&&strokes_<MaxStrokes&&points_<MaxTotal;} /* was strokes_<256: a stale pre-19d cap that stopped ink in long notes */
 unsigned unsaved()const noexcept{unsigned n=0;for(const auto& slot:*slots_){auto state=slot.state.load(std::memory_order_acquire);if(state==Active||state==Queued||state==Failed)++n;}return n;}
 bool failed()const noexcept{return failed_;}bool capacity()const noexcept{return capacity_||strokes_>=MaxStrokes||points_>=MaxTotal;}
 void install(const Window&,std::span<const std::uint32_t>,unsigned strokes,unsigned points);
 void clearForOpen(const Window& w){product::require(!contact_&&!pending_,"note-open-while-writing");std::fill(scratch_.begin(),scratch_.end(),0);install(w,scratch_,0,0);}
 std::span<const std::uint32_t> capturedInk()noexcept;const Window& window()const noexcept{return window_;}
 spike::Surface surface()noexcept{return {scratch_,1620,2160,1632};}
 spike::Rect damage()const noexcept{auto dirty=raster_.damage();if(generated_){auto part=generated_->damage();if(!part.empty()){part.x0+=generatedArea_.x0;part.x1+=generatedArea_.x0;part.y0+=generatedArea_.y0;part.y1+=generatedArea_.y0;dirty.include(part);}}return dirty;}void clearDamage()noexcept{raster_.clearDamage();if(generated_)generated_->clearDamage();}
 // An accepted partial/unrelated update must never retire unpresented ink.
 void presentationCovered(spike::Rect accepted)noexcept{auto dirty=damage();if(!dirty.empty()&&accepted.x0<=dirty.x0&&accepted.y0<=dirty.y0&&accepted.x1>=dirty.x1&&accepted.y1>=dirty.y1)clearDamage();}
 void composite(spike::Surface out,std::span<const std::uint32_t> paper,spike::Rect r)const noexcept;
};
std::vector<std::uint32_t> render(const QJsonArray& strokes,const Window&);
}
