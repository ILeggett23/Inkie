// SPDX-License-Identifier: AGPL-3.0-only
#include "vendor.h"
#include <algorithm>
namespace spike {
bool Pixels::validFerrari()const noexcept{return data&&reinterpret_cast<std::uintptr_t>(data)%4==0&&width==1620&&height==2160&&format==4&&stride==6528;}
void CaptureTable::observe(Pixels p)noexcept{
 if(p.format==24)return; // expected independent mask; never exposed for drawing
 if(!p.validFerrari()){failed_=true;return;}
 for(std::size_t i=0;i<used_;++i)if(entries_[i].data==p.data){if(entries_[i].stride!=p.stride||entries_[i].format!=p.format)failed_=true;return;}
 if(used_==entries_.size()){failed_=true;return;}entries_[used_++]=p;
}
bool CaptureTable::select(Pixels&p)const noexcept{if(failed_||used_!=1)return false;p=entries_[0];return true;}
bool VendorDisplay::initialize(const RecoveryLease&lease)noexcept {
 if(active_||retired_||!lease.valid()||lease.stopRequested()||!monotonic()||monotonic()>=lease.deadline())return false;
 lease_=&lease;inside_=true;bool ok=calls_.acquire(pixels_);inside_=false;
 active_=ok&&pixels_.validFerrari()&&lease.valid()&&monotonic()<lease.deadline();if(!active_)retired_=true;return active_;
}
Surface VendorDisplay::surface()const noexcept {
 if(!active_||retired_||inside_||!lease_||!lease_->valid())return {};
 return {{reinterpret_cast<std::uint32_t*>(pixels_.data),std::size_t(pixels_.stride/4)*pixels_.height},pixels_.width,pixels_.height,int(pixels_.stride/4)};
}
bool VendorDisplay::submitChecked(Rect r,int mode,int flags)noexcept{
 if(!active_||retired_||inside_||!lease_->valid()||lease_->stopRequested()||monotonic()>=lease_->deadline()||r.empty()||r.x0<0||r.y0<0||r.x1>pixels_.width||r.y1>pixels_.height)return false;
 auto area=std::uint64_t(r.x1-r.x0)*std::uint64_t(r.y1-r.y0);
 if(flags==0&&area>1620ULL*2160/4)return false; // reject an excessive live region; no full-page live redraw
 if(submissions_==MaxSubmissions||area>MaxArea-area_){retired_=true;return false;}
 ++submissions_;area_+=area;inside_=true;
 calls_.region(r,0,mode,flags); // void: no invented Accepted/Busy/status from x0
 inside_=false;return true;
}
bool VendorDisplay::initialPage()noexcept{
 Surface s=surface();if(!s.valid()||submissions_!=0)return false;
 // Initialize only visible pixels; padding is not handwriting or damage.
 for(int y=0;y<s.height;++y)std::fill_n(s.pixels.data()+std::size_t(y)*s.stridePixels,s.width,0xffffffffu);
 return submitChecked({0,0,s.width,s.height},3,1);
}
Submit VendorDisplay::submit(Rect r)noexcept{return submitChecked(r,0,0)?Submit::Accepted:Submit::Failed;}
bool VendorDisplay::initialPreparedPage(std::span<const std::uint32_t> prepared)noexcept{
 Surface s=surface();if(!s.valid()||submissions_!=0||prepared.size()!=s.pixels.size())return false;
 if(prepared.data()!=s.pixels.data())std::copy(prepared.begin(),prepared.end(),s.pixels.begin());
 return submitChecked({0,0,s.width,s.height},3,1);
}
void VendorDisplay::pump()noexcept{if(active_&&!retired_&&!inside_&&lease_->valid()&&!lease_->stopRequested()&&monotonic()<lease_->deadline()){inside_=true;calls_.events();inside_=false;}}
void VendorDisplay::shutdown()noexcept{active_=false;retired_=true;pixels_={};lease_=nullptr;}
}
