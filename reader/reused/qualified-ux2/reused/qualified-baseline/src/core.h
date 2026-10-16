// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace spike {
using Ns=std::uint64_t;
Ns monotonic() noexcept;
Ns threadCpu() noexcept;
enum class Phase { Down, Move, Up };
struct Sample {int x{},y{}; Phase phase{}; Ns receipt{};int pressure{};bool pressurePresent{};bool eraser{};};
struct Point {int x{},y{}; Phase phase{}; Ns receipt{};bool eraser{};};
struct Rect {
 int x0{},y0{},x1{},y1{}; // half-open pixel coordinates
 bool empty()const noexcept{return x1<=x0||y1<=y0;}
 void include(Rect other) noexcept;
 bool contains(int x,int y)const noexcept{return x>=x0&&x<x1&&y>=y0&&y<y1;}
};
struct Axis {int min{},max{};};
struct Transform {
 Axis x,y;int width{},height{},rotation{};bool mirrorX{};
 bool valid()const noexcept;
 bool map(const Sample&,Point&)const noexcept;
};
template<class T,std::size_t N> class Spsc {
 static_assert(N>1);
 std::array<T,N> values_{};
 alignas(64) std::atomic<std::size_t> head_{0};
 alignas(64) std::atomic<std::size_t> tail_{0};
public:
 bool push(T value) noexcept {
  auto h=head_.load(std::memory_order_relaxed);auto n=(h+1)%N;
  if(n==tail_.load(std::memory_order_acquire))return false;
  values_[h]=value;head_.store(n,std::memory_order_release);return true;
 }
 bool pop(T& value) noexcept {
  auto t=tail_.load(std::memory_order_relaxed);
  if(t==head_.load(std::memory_order_acquire))return false;
  value=values_[t];tail_.store((t+1)%N,std::memory_order_release);return true;
 }
};
using InputQueue=Spsc<Sample,8193>;
// RGB32 surface is borrowed, not owned. No buffer swap or inferred completion.
struct Surface {
 std::span<std::uint32_t> pixels;int width{},height{},stridePixels{};
 bool valid()const noexcept;
};
Rect segment(Surface,Point,Point,int radius,std::uint32_t color=0xff000000u) noexcept;
enum class Submit { Accepted, Busy, Failed };
struct Display {
 virtual Submit submit(Rect) noexcept=0;
 virtual ~Display()=default;
};
struct Timing {Ns t0{},t1{},t2{},t3{},t4{},cpu{};};
class Engine {
 static constexpr std::size_t Capacity=16384;
 Transform transform_;Surface surface_;Display& display_;int radius_;
 std::array<Point,Capacity> points_{};
 std::array<Timing,Capacity> timing_{};
 std::size_t size_{},submitted_{};Point previous_{};
 bool contact_{},failed_{};Rect dirty_{};
public:
 std::uint64_t busy{},submitFailures{},rejected{};
 Engine(Transform,Surface,Display&,int radius=2) noexcept;
 bool accept(Sample) noexcept;
 Submit flush() noexcept;
 void inputFailed() noexcept {contact_=false;failed_=true;}
 bool failed()const noexcept{return failed_;}
 Rect dirty()const noexcept{return dirty_;}
 std::span<const Point> points()const noexcept{return {points_.data(),size_};}
 std::span<const Timing> timing()const noexcept{return {timing_.data(),size_};}
 std::size_t submitted()const noexcept{return submitted_;}
};
struct FakeDisplay final:Display {
 Submit outcome{Submit::Accepted};std::uint64_t calls{};Rect delivered{};
 Submit submit(Rect r)noexcept override{++calls;if(outcome==Submit::Accepted)delivered.include(r);return outcome;}
};
}
