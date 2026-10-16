// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "work_tracking.h"
#include "session.h"
#include "styled_ink.h"
#include <optional>
#include <QJsonArray>
#include <QJsonObject>
#include <memory>
#include <vector>
namespace tom {
// Native lower sheet (redesign4): the areas moved, their sizes did not (1480x680 compact, 1480x1360 expanded);
// v2 strokes are area-relative, so existing drafts land in the same place on the paper.
inline constexpr spike::Rect WritingArea{70,1360,1550,2040},ExpandedWritingArea{70,680,1550,2040};
// Strokes written before tom-logical-v2 carry absolute points of the original compact area.
inline constexpr spike::Rect LegacyWritingArea{70,1110,1550,1790};
inline constexpr int LogicalWidth=1480,LogicalHeight=1360;
// D2 capacity (raised from 64 strokes / 32768 points per question); shared by the canvas and loaders.
inline constexpr unsigned MaxStrokes=1024,MaxTotalPoints=262144;
class Canvas final:public spike::OperationSink {
 static_assert(std::atomic<unsigned>::is_always_lock_free&&std::atomic<bool>::is_always_lock_free,"Tom contact slots require lock-free atomics");
 static constexpr unsigned Slots=16,MaxPoints=2048,MaxTotal=MaxTotalPoints; // 16 strokes may wait for their save (was 4)
 enum State:unsigned{Free,Active,Queued,Taken,Failed};
 struct Stroke {std::array<spike::Point,MaxPoints> points{};unsigned count{};bool ignored{},started{},eraser{},interrupted{};styled::Style style{};std::atomic<unsigned> state{Free};recovery::v2::progress::WorkToken tracking;};
 reader::lifecycle::Tracking tracking_;
 std::unique_ptr<std::array<Stroke,Slots>> slots_;spike::Spsc<unsigned,Slots+1> queue_;int active_{-1};
 std::atomic<unsigned> pending_{0},points_{0},strokes_{0};std::atomic<bool> enabled_{false},contact_{false},capacity_{false},failed_{false};
 std::vector<std::uint32_t> scratch_,pixels_;spike::Rect area_{WritingArea},dirty_{};int scroll_{};std::uint64_t damageGeneration_{};
 styled::Style style_{styled::Tool::Fineliner,styled::Width::Medium,0};bool eraser_{};void draw(spike::Point,spike::Point,bool,int radius=2)noexcept;
public:
 struct StrokeView {std::span<const spike::Point> points;bool eraser{},interrupted{};std::optional<styled::Style> style{};};
 bool setStyle(styled::Style s)noexcept{if(contact_||unsigned(s.tool)>=8||unsigned(s.width)>=3||s.color)return false;style_=s;eraser_=false;return true;}styled::Style style()const noexcept{return style_;}bool setEraser(bool value)noexcept{if(contact_)return false;eraser_=value;return true;}bool eraser()const noexcept{return eraser_;}
 Canvas(reader::lifecycle::Tracking tracking={});reader::lifecycle::Tracking tracking()const noexcept{return tracking_;}bool reserve()noexcept override;bool append(const spike::Point&)noexcept override;void cancel()noexcept override;void stop()noexcept;
 // Worker-only consumer. The POD span stays valid until durable acknowledge(slot,true).
 bool take(unsigned& slot,StrokeView&)noexcept;
 bool take(unsigned& slot,QJsonObject&); // Worker-only JSON convenience; never contact code.
 bool retained(unsigned slot,StrokeView&)const noexcept;
 void acknowledge(unsigned slot,bool saved)noexcept;
 // D2: points the current stroke piece can still take (0 when no stroke is active). The display
 // closes a piece before it reaches this bound and continues the stroke in a new piece.
 std::size_t room()const noexcept;
 void enable(bool v)noexcept{enabled_=v;}bool contact()const noexcept{return contact_;}bool active()const noexcept{return active_>=0;}
 bool writable()const noexcept{return enabled_&&!failed_&&!capacity_&&pending_<Slots&&strokes_<MaxStrokes&&points_<MaxTotal;}
 unsigned pending()const noexcept{return pending_;}unsigned unsaved()const noexcept;bool failed()const noexcept{return failed_;}bool capacity()const noexcept{return capacity_||strokes_>=MaxStrokes||points_>=MaxTotal;}
 void restore(const QJsonArray&,bool expanded=false,bool lowerHalf=false); // Intercontact, no unsaved slots.
 spike::Rect area()const noexcept{return area_;}spike::Surface surface()noexcept{return {scratch_,1620,2160,1632};}
 spike::Rect damage()const noexcept{return dirty_;}std::uint64_t damageGeneration()const noexcept{return damageGeneration_;}
 // Only exact, accepted coverage of this generation retires damage; Busy/partial/old frames do not.
 void presentationCovered(spike::Rect accepted,std::uint64_t generation)noexcept;
 void composite(spike::Surface,std::span<const std::uint32_t> paper,spike::Rect)const noexcept;
};
QJsonObject handwritingStroke(const Canvas::StrokeView&); // Worker-only; ordinary fixed slot input.
QByteArray handwritingPng(const QJsonArray&); // Worker-only; bounded 740x680 PNG, stable logical geometry.
}
