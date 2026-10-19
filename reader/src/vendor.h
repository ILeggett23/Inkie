// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "session.h"
#include "refresh_policy.h"
#include <span>
#include <algorithm>
// Pinned converter footprint; update geometry remains unchanged.
namespace spike {inline Rect conversionFootprint(Rect r,int width=1620,int height=2160)noexcept{const int left=r.x0&~3,top=r.y0&~1;return {left,top,std::min(width,left+((((r.x1+3)&~3)-left+7)&~7)),std::min(height,(r.y1+1)&~1)};}}
#include <vector>
namespace spike {
struct Pixels {unsigned char* data{};int width{},height{},format{};std::int64_t stride{};bool validFerrari()const noexcept;bool validFerrariMask()const noexcept;};
class CaptureTable {
 std::array<Pixels,8> entries_{};std::size_t used_{};bool failed_{};Pixels mask_{};bool maskSeen_{},maskFailed_{};
public:
 void observe(Pixels)noexcept;
 bool select(Pixels&)const noexcept;bool selectMask(Pixels&)const noexcept;
};
// Temporary source-mask bytes are restored after the pinned synchronous
// snapshot/conversion join. This object performs no allocation or vendor call.
class ScopedEraseMask final {
 Pixels mask_;Rect region_;std::span<unsigned char> saved_;bool applied_{};
public:
 ScopedEraseMask(Pixels,std::span<const unsigned char>,int stride,Rect,std::span<unsigned char>)noexcept;
 ~ScopedEraseMask();ScopedEraseMask(const ScopedEraseMask&)=delete;ScopedEraseMask& operator=(const ScopedEraseMask&)=delete;
 explicit operator bool()const noexcept{return applied_;}
};
struct VendorCalls {
 virtual bool acquire(Pixels&)noexcept=0;
 virtual void region(Rect,int content,int mode,int flags)noexcept=0;
 // Boolean is local mask admission only; the vendor swap itself is void.
 virtual bool eraseRegion(Rect r,int content,int mode,int flags,std::span<const unsigned char>,int)noexcept{region(r,content,mode,flags);return true;}
 virtual void events()noexcept=0;
 virtual ~VendorCalls()=default;
};
// Owns no vendor memory; normal return authorizes serial SOURCE reuse only.
class VendorDisplay final:public SessionDisplay {
 VendorCalls& calls_;RecoveryLease const* lease_{};Pixels pixels_{};bool active_{},retired_{},inside_{};
 std::array<std::uint64_t,unsigned(ux::FullRefreshReason::Recovery)> completeReasons_{};std::uint32_t submissions_{};std::uint64_t area_{};std::vector<std::uint32_t> beforeErase_;std::vector<unsigned char> eraseMask_;Rect eraseSnapshot_{};
 // Paper Pro (libqsgepaper acep2) swapBuffers(QRect, EPContentType, EPScreenMode, UpdateFlags), read from the
 // library's own tables: content Mono=0 Grayscale=1 Color=3; screen mode Pen=0 Mono=1 Animation=2 UI=3 Content=4.
 // UpdateFlags 1 = full (flashing) update. Until D2 every update was sent as Mono content.
 // ModeContent (4) is used only for regional removal cleanup (cleanContent). Full pages stay in UI mode: on the Paper Pro
 // a Content-mode complete page swap after the first one blocked the
 // main thread forever (run 13, session 2b4e36d0); full pages use UI mode (3) as in every earlier run.
 // Regional quality classifies composed RGB as Mono/Grayscale/Color and uses
 // UI3/flags0. The owned maintenance timer is adapted to defer a busy batch lock
 // instead of blocking the foreground event pump; maintenance stays enabled.
 // Stock's ordinary renderer retains regional UI quality with flags0. Color
 // cleanup must preserve RGB; the Mono screen mode is not a color replacement.
 // UI3/flags0 idle safety remains a qualification requirement: historical runs
 // 13-15 stalled in sendTModeUpdate while acquiring the Swtcon batch mutex.
 enum Content:int {ContentMono=0,ContentGrayscale=1,ContentColor=3};
 enum Mode:int {ModePen=0,ModeMono=1,ModeAnimation=2,ModeUi=3,ModeContent=4};
 bool ready(Rect)const noexcept;
 int qualityContent(Rect)const noexcept;
 bool submitChecked(Rect,int content,int mode,int flags,ux::FullRefreshReason reason=ux::FullRefreshReason::None)noexcept;
public:
 static constexpr std::uint32_t MaxSubmissions=512;
 static constexpr std::uint64_t MaxArea=8ULL*1620*2160;
 explicit VendorDisplay(VendorCalls&c):calls_(c){}
 bool initialize(const RecoveryLease&)noexcept override;
 Surface surface()const noexcept override;
 bool initialPage()noexcept override;
 bool initialPreparedPage(std::span<const std::uint32_t>)noexcept;
 bool readerPage(ux::FullRefreshReason)noexcept;
 Submit submit(Rect)noexcept override;
 // Pinned Pen bit1 selects lower conversion14, also observed for stock erase.
 // This is a verified behavioral flag, not a recovered vendor enum name.
 bool beginErase(Rect)noexcept;Submit erase(Rect)noexcept;
 // Regional RGB-preserving quality; never deliberately sets the full-image bit.
 Submit clean(Rect)noexcept;
 // Removal quality retains the richer of the old footprint and new pixels.
 int removalContent(Rect r)const noexcept{return ready(r)?qualityContent(r):ContentMono;}
 Submit cleanRemoval(Rect,int previousContent)noexcept;
 // cleanRemoval in Content mode: the waveform stock uses to clean erased ink (see vendor.cpp).
 Submit cleanContent(Rect,int previousContent)noexcept;
 // Regional UI quality, sharing clean()'s RGB and extent semantics.
 Submit ui(Rect)noexcept;
 void pump()noexcept override;
 void shutdown()noexcept override;
 std::uint32_t submissions()const noexcept{return submissions_;}
 std::uint64_t completeRequests(ux::FullRefreshReason reason)const noexcept{return ux::completeRefreshAllowed(reason)?completeReasons_[unsigned(reason)-1]:0;}
};
VendorCalls& realVendorCalls()noexcept;
}
