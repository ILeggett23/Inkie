// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
namespace recovery::v2::sessionpower {
enum class WakeCause:unsigned {None,DoubleTap,PenUndocked,PenTip};
inline constexpr const char* NativeHwmonDevice="devices/platform/soc@0/30800000.bus/30800000.spba-bus/30860000.serial/30860000.serial:0/30860000.serial:0.0/serial0/serial0-0";
class NativeWakeSources final {
 struct Counter {std::filesystem::path path;std::uint64_t value{};bool valid{};};std::filesystem::path root_;std::array<Counter,2> counters_{};bool read(const Counter&,std::uint64_t&)const noexcept;
public:
 explicit NativeWakeSources(std::filesystem::path root="/sys")noexcept:root_(std::move(root)){}void baseline()noexcept;WakeCause afterResume()noexcept;
};
std::uint64_t bootTime()noexcept;
class DoubleTapWake final {
 std::uint64_t cutoff_{},first_{};int x_{},y_{};
public:
 void reset(std::uint64_t cutoff)noexcept{cutoff_=cutoff;first_=0;}
 bool tap(unsigned contacts,bool still,int x,int y,std::uint64_t started,std::uint64_t ended,std::uint64_t now)noexcept;
};
}
