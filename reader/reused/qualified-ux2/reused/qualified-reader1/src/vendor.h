// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "session.h"
namespace spike {
struct Pixels {unsigned char* data{};int width{},height{},format{};std::int64_t stride{};bool validFerrari()const noexcept;};
class CaptureTable {
 std::array<Pixels,8> entries_{};std::size_t used_{};bool failed_{};
public:
 void observe(Pixels)noexcept;
 bool select(Pixels&)const noexcept;
};
struct VendorCalls {
 virtual bool acquire(Pixels&)noexcept=0;
 virtual void region(Rect,int content,int mode,int flags)noexcept=0;
 virtual void events()noexcept=0;
 virtual ~VendorCalls()=default;
};
// Owns no vendor memory; normal return authorizes serial SOURCE reuse only.
class VendorDisplay final:public SessionDisplay {
 VendorCalls& calls_;RecoveryLease const* lease_{};Pixels pixels_{};bool active_{},retired_{},inside_{};
 std::uint32_t submissions_{};std::uint64_t area_{};
 bool submitChecked(Rect,int,int)noexcept;
public:
 static constexpr std::uint32_t MaxSubmissions=512;
 static constexpr std::uint64_t MaxArea=8ULL*1620*2160;
 explicit VendorDisplay(VendorCalls&c):calls_(c){}
 bool initialize(const RecoveryLease&)noexcept override;
 Surface surface()const noexcept override;
 bool initialPage()noexcept override;
 bool initialPreparedPage(std::span<const std::uint32_t>)noexcept;
 bool readerPage()noexcept;
 Submit submit(Rect)noexcept override;
 void pump()noexcept override;
 void shutdown()noexcept override;
 std::uint32_t submissions()const noexcept{return submissions_;}
};
VendorCalls& realVendorCalls()noexcept;
}
