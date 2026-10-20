// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "localized_reveal.h"
#include <array>
#include <span>
#include <vector>
namespace sand {
using Rect=localized::Rect;using Identity=localized::Identity;
enum class Direction:unsigned {LeftToRight,RightToLeft};
enum class Quality:unsigned {Disabled,Minimal,Reduced,Full};
enum class State:unsigned {Hidden,Preparing,Revealing,Visible,Dismissing};
enum class Reason:unsigned {None,Disabled,Motion,Contact,Selection,Work,Power,Congestion,Memory,Geometry,Scene,Clock,Timeout,Cost,Interrupted};
struct Policy {
 Quality quality{Quality::Disabled};bool reducedMotion{},contact{},selecting{},heavy{},power{},congested{},memoryTight{},expensive{};
};
struct Request {Rect target,screen;Identity identity;Direction direction{Direction::RightToLeft};std::uint32_t seed{1};bool dismiss{};unsigned durationMs{280};};
struct Update {Rect damage;std::uint64_t stamp{};unsigned stage{};bool intermediate{};explicit operator bool()const noexcept{return stamp&&!damage.empty();}};
struct Metrics {std::uint64_t starts{},masks{},requests{},refused{},accepted{},skipped{},cancelled{},cleanups{},generationNs{},paintNs{},lastPaintNs{},worstPaintNs{};std::size_t cacheBytes{};Quality quality{Quality::Disabled};Reason reason{Reason::None};};
// Pure CPU composition, with one cached old-scene crop and one outstanding
// source stage. This never owns a display, sleeps, or equates acceptance to a
// physical fence. The caller composes the CURRENT canonical scene before paint.
class Transition final {
 struct Grain {int x{},y{},travel{},drift{};unsigned variant{};};
 std::array<Grain,256> grains_{};
 std::vector<std::uint32_t> before_;
 std::array<Rect,64> uncovered_{};unsigned uncoveredCount_{};
 Request request_{};Rect region_{},paintClip_{};Policy policy_{};Update update_{};Metrics metrics_{};
 State state_{State::Hidden};std::uint64_t serial_{},started_{},lastTime_{};
 unsigned stages_{},stage_{},duration_{},grainCount_{};bool captured_{},painted_{},finishing_{};
 Update issue(unsigned stage,bool intermediate)noexcept;
 void finish(Reason)noexcept;
public:
 static constexpr unsigned MaxPixels=2*1024*1024,MaxDurationMs=350,Trail=32;
 static constexpr std::size_t MaxCacheBytes=std::size_t(MaxPixels)*sizeof(std::uint32_t);
 static Quality choose(Policy,std::uint64_t area,Reason&)noexcept;
 bool start(Request,Policy,std::uint64_t nowMs)noexcept;
 // Snapshot the old, software-submitted scene BEFORE canonical composition.
 bool capture(std::span<const std::uint32_t>,int width,int height,int stride,std::uint64_t nowMs)noexcept;
 // One deterministic reservoir of destinations on the real component artwork;
 // no atlas scaling, alpha fade, extra final-image cache or particle simulation.
 bool buildSilhouette(std::span<const std::uint32_t> canonical,int width,int height,int stride)noexcept;
 Update advance(Identity,Policy,std::uint64_t nowMs)noexcept;
 bool paint(std::uint64_t,std::span<std::uint32_t>,int width,int height,int stride,Rect clip)noexcept;
 bool acknowledge(std::uint64_t,Rect)noexcept;
 void cancel(Reason why=Reason::Interrupted)noexcept{finish(why);}
 void finishImmediately()noexcept{finish(Reason::Interrupted);}
 void reset()noexcept{finish(Reason::Interrupted);} // never discards painted debt
 bool needsCapture()const noexcept{return state_==State::Preparing&&!captured_;}
 bool prepareIdentity(Identity id)noexcept{if(!needsCapture()||id.ui!=request_.identity.ui)return false;request_.identity=id;return true;}
 bool active()const noexcept{return state_==State::Preparing||state_==State::Revealing||state_==State::Dismissing;}
 bool pending()const noexcept{return active()||bool(update_)||finishing_;}
 bool cleanupPending()const noexcept{return finishing_;}
 State state()const noexcept{return state_;}Rect region()const noexcept{return region_;}
 Identity identity()const noexcept{return request_.identity;}Update update()const noexcept{return update_;}
 void submitted(bool accepted)noexcept{++metrics_.requests;if(!accepted)++metrics_.refused;}
 const Metrics& metrics()const noexcept{return metrics_;}
};
}
