#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace product {struct AnimationPolicy;}
namespace localized {
struct Rect {
 int x0{},y0{},x1{},y1{};
 constexpr bool empty()const noexcept{return x0>=x1||y0>=y1;}
 constexpr bool operator==(const Rect&)const noexcept=default;
};
struct Identity {std::uint64_t page{},ui{};constexpr bool operator==(const Identity&)const noexcept=default;};
struct Request {Rect popup,screen;Identity identity;bool userEnabled{false};};
struct Update {
 Rect damage;std::uint64_t stamp{};unsigned frame{};bool paint{},active{};
 explicit operator bool()const noexcept{return stamp!=0&&!damage.empty();}
};
// Pure optional adornment outside the popup's readable content. The caller owns
// the clock, current static scene, and existing display submission path.
// Restore Update.damage from the CURRENT static scene before each paint/cleanup.
// Acknowledge only after the existing display accepts a source submission covering
// the entire update. This is source acceptance, never optical completion.
class Reveal final {
 struct Dot {int x{},y{};};
 std::array<Dot,18> dots_{};
 Request request_{};Rect region_{},painted_{};Update issued_{};
 std::uint64_t started_{},lastTime_{},serial_{};
 unsigned particles_{},frames_{},duration_{},frame_{~0u};
 bool active_{},issuedValid_{},paintedForStamp_{},contactBlocked_{};
 void cancelLogical()noexcept;
 Update issue(bool paint,unsigned frame)noexcept;
 bool permitted(const product::AnimationPolicy&)const noexcept;
public:
 static constexpr unsigned MaxParticles=18,MaxFrames=3,MaxDurationMs=180,MaxArea=300000;
 // UINT64_MAX represents an unavailable clock. Zero is a valid monotonic origin.
 // New starts refuse until prior painted or unacknowledged cleanup is retired.
 bool start(const Request&,const product::AnimationPolicy&,std::uint64_t nowMs)noexcept;
 Update advance(Identity,const product::AnimationPolicy&,std::uint64_t nowMs)noexcept;
 bool paint(std::uint64_t stamp,std::span<std::uint32_t> pixels,int width,int height,int stride)noexcept;
 bool acknowledgePresented(std::uint64_t stamp,Rect acceptedRect)noexcept;
 bool active()const noexcept{return active_;}
 bool cleanupPending()const noexcept{return !active_&&!painted_.empty();}
 bool quiescent()const noexcept{return !active_&&painted_.empty()&&!issuedValid_;}
 Rect region()const noexcept{return region_;}
 Identity identity()const noexcept{return request_.identity;}
 unsigned particleCount()const noexcept{return particles_;}
 unsigned frameCount()const noexcept{return frames_;}
 unsigned durationMs()const noexcept{return duration_;}
};
}
