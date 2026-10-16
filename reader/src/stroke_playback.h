// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "styled_ink.h"
#include "native_ink.h"
#include <chrono>
namespace tomink {
class PlaybackTarget {public:virtual ~PlaybackTarget()=default;virtual bool contact()const noexcept=0;virtual unsigned pending()const noexcept=0;virtual bool playBegin(styled::Style)noexcept=0;virtual bool playPoint(styled::Coordinate,bool)noexcept=0;virtual void playEnd()noexcept=0;virtual void playFill(const QJsonObject&)=0;};
enum class PlaybackSpeed:unsigned {Quick,Normal,Deliberate};
struct PlaybackConfig {double pixelsPerSecond{520},accelerationTravel{12000},minimumMs{1000},maximumMs{45000};unsigned intervalMs{40},pointBudget{256};};
struct PlaybackStroke {styled::Style style;std::vector<styled::Coordinate> points;double rate{1},travel{},durationMs{},pauseAfterMs{};unsigned source{};std::shared_ptr<const QJsonObject> fill;};
struct PlaybackPlan {QString group;QRectF bounds;double travel{},durationMs{};PlaybackConfig config;std::vector<PlaybackStroke> strokes;};
std::shared_ptr<const PlaybackPlan> playbackPlan(const QString& group,const QJsonArray&,const std::vector<nativeink::StrokeTiming>& timing={},PlaybackConfig config={});
class StrokePlayback {
 std::shared_ptr<const PlaybackPlan> plan_;std::size_t stroke_{},point_{};styled::Coordinate cursor_{};bool begun_{},penOpen_{};double pauseMs_{};PlaybackSpeed speed_{PlaybackSpeed::Normal};std::chrono::steady_clock::time_point next_{},last_{};
public:
 void start(std::shared_ptr<const PlaybackPlan> p){plan_=std::move(p);stroke_=point_=0;begun_=penOpen_=false;pauseMs_=0;next_=last_={};}void clear()noexcept{plan_.reset();begun_=penOpen_=false;pauseMs_=0;next_=last_={};}std::shared_ptr<const PlaybackPlan> detach()noexcept{auto retired=std::move(plan_);begun_=penOpen_=false;pauseMs_=0;next_=last_={};return retired;}bool active()const noexcept{return plan_&&(stroke_<plan_->strokes.size()||pauseMs_>0);}bool quietForCleanup()const noexcept{return !penOpen_&&pauseMs_>=120;}void speed(PlaybackSpeed v)noexcept{speed_=v;}void suspend(PlaybackTarget&)noexcept;void finish(PlaybackTarget& canvas)noexcept{suspend(canvas);if(plan_)stroke_=plan_->strokes.size();point_=0;begun_=penOpen_=false;pauseMs_=0;}bool extend(std::shared_ptr<const PlaybackPlan>);QJsonArray visibleStrokes()const;styled::Coordinate position()const noexcept{return cursor_;}double durationMs()const noexcept{return plan_?plan_->durationMs*(speed_==PlaybackSpeed::Quick?.65:speed_==PlaybackSpeed::Deliberate?1.5:1):0;}bool tick(PlaybackTarget&,std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now());
};
}
