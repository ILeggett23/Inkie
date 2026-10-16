// SPDX-License-Identifier: AGPL-3.0-only
#include "vendor.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace spike {
bool Pixels::validFerrari()const noexcept{return data&&reinterpret_cast<std::uintptr_t>(data)%4==0&&width==1620&&height==2160&&format==4&&stride==6528;}
bool Pixels::validFerrariMask()const noexcept{return data&&width==1620&&height==2160&&format==24&&stride==1632;}
ScopedEraseMask::ScopedEraseMask(Pixels mask,std::span<const unsigned char> source,int stride,Rect region,std::span<unsigned char> saved)noexcept:mask_(mask),region_(region),saved_(saved){
 if(!mask.validFerrariMask()||stride!=1632||region.empty()||region.x0<0||region.y0<0||region.x1>1620||region.y1>2160||source.size()<std::size_t(1632)*2160||saved.size()<std::size_t(1632)*2160)return;
 region_=conversionFootprint(region);
 // Aligned converter reads must see zero mask outside the requested damage,
 // rather than mask bytes left by a previous operation. Restore every byte.
 for(int y=region_.y0;y<region_.y1;++y){const auto at=std::size_t(y)*1632+region_.x0;const auto n=std::size_t(region_.x1-region_.x0);std::memcpy(saved.data()+at,mask.data+at,n);std::memset(mask.data+at,0,n);}
 for(int y=region.y0;y<region.y1;++y){const auto at=std::size_t(y)*1632+region.x0;std::memcpy(mask.data+at,source.data()+at,std::size_t(region.x1-region.x0));}applied_=true;
}
ScopedEraseMask::~ScopedEraseMask(){if(applied_)for(int y=region_.y0;y<region_.y1;++y){const auto at=std::size_t(y)*1632+region_.x0;std::memcpy(mask_.data+at,saved_.data()+at,std::size_t(region_.x1-region_.x0));}}
void CaptureTable::observe(Pixels p)noexcept{
 if(p.format==24){if(!p.validFerrariMask()){maskFailed_=true;return;}if(maskSeen_&&mask_.data!=p.data){maskFailed_=true;return;}mask_=p;maskSeen_=true;return;} // independent, separately validated erase mask
 if(!p.validFerrari()){failed_=true;return;}
 for(std::size_t i=0;i<used_;++i)if(entries_[i].data==p.data){if(entries_[i].stride!=p.stride||entries_[i].format!=p.format)failed_=true;return;}
 if(used_==entries_.size()){failed_=true;return;}entries_[used_++]=p;
}
bool CaptureTable::select(Pixels&p)const noexcept{if(failed_||used_!=1)return false;p=entries_[0];return true;}
bool CaptureTable::selectMask(Pixels&p)const noexcept{if(maskFailed_||!maskSeen_)return false;p=mask_;return true;}
bool VendorDisplay::initialize(const RecoveryLease&lease)noexcept {
 if(active_||retired_||!lease.valid()||lease.stopRequested()||!monotonic()||monotonic()>=lease.deadline())return false;
 lease_=&lease;inside_=true;bool ok=calls_.acquire(pixels_);inside_=false;
 active_=ok&&pixels_.validFerrari()&&lease.valid()&&monotonic()<lease.deadline();if(active_)try{beforeErase_.resize(std::size_t(1632)*2160);eraseMask_.resize(std::size_t(1632)*2160);}catch(...){active_=false;}if(!active_)retired_=true;return active_;
}
Surface VendorDisplay::surface()const noexcept {
 if(!active_||retired_||inside_||!lease_||!lease_->valid())return {};
 return {{reinterpret_cast<std::uint32_t*>(pixels_.data),std::size_t(pixels_.stride/4)*pixels_.height},pixels_.width,pixels_.height,int(pixels_.stride/4)};
}
bool VendorDisplay::ready(Rect r)const noexcept{
 return active_&&!retired_&&!inside_&&lease_&&lease_->valid()&&!lease_->stopRequested()&&monotonic()<lease_->deadline()&&!r.empty()&&r.x0>=0&&r.y0>=0&&r.x1<=pixels_.width&&r.y1<=pixels_.height;
}
bool VendorDisplay::submitChecked(Rect r,int content,int mode,int flags)noexcept{
 if(!ready(r))return false;
 auto area=std::uint64_t(r.x1-r.x0)*std::uint64_t(r.y1-r.y0);
 if(mode==ModePen&&area>1620ULL*2160/4)return false; // reject an excessive live (pen) region; no full-page live redraw
 // V1 bounded sessions keep the lifetime update budget. D2 user-ended (sustained) sessions have no
 // lifetime budget: a session ends only on the power button, never because the screen updated often.
 const bool sustained=lease_->sustained().enabled();
 if(!sustained&&(submissions_==MaxSubmissions||area>MaxArea-area_)){retired_=true;return false;}
 if(submissions_<UINT32_MAX)++submissions_;area_=area>UINT64_MAX-area_?UINT64_MAX:area_+area;inside_=true;
 bool admitted=true;if(flags==2)admitted=calls_.eraseRegion(r,content,mode,flags,eraseMask_,1632);else calls_.region(r,content,mode,flags); // vendor swap remains void
 inside_=false;return admitted;
}
bool VendorDisplay::initialPage()noexcept{
 Surface s=surface();if(!s.valid()||submissions_!=0)return false;
 // Initialize only visible pixels; padding is not handwriting or damage.
 for(int y=0;y<s.height;++y)std::fill_n(s.pixels.data()+std::size_t(y)*s.stridePixels,s.width,0xffffffffu);
 return submitChecked({0,0,s.width,s.height},ContentMono,ModeUi,1);
}
Submit VendorDisplay::submit(Rect r)noexcept{return submitChecked(r,ContentMono,ModePen,0)?Submit::Accepted:Submit::Failed;}
bool VendorDisplay::beginErase(Rect r)noexcept{eraseSnapshot_={};if(!ready(r)||beforeErase_.size()!=std::size_t(1632)*2160)return false;const auto* source=reinterpret_cast<const std::uint32_t*>(pixels_.data);for(int y=r.y0;y<r.y1;++y)std::copy_n(source+std::size_t(y)*1632+r.x0,r.x1-r.x0,beforeErase_.data()+std::size_t(y)*1632+r.x0);eraseSnapshot_=r;return true;}
Submit VendorDisplay::erase(Rect r)noexcept{if(!ready(r)||std::uint64_t(r.x1-r.x0)*std::uint64_t(r.y1-r.y0)>1620ULL*2160/4||eraseSnapshot_.empty()||r.x0<eraseSnapshot_.x0||r.y0<eraseSnapshot_.y0||r.x1>eraseSnapshot_.x1||r.y1>eraseSnapshot_.y1)return Submit::Failed;const auto* source=reinterpret_cast<const std::uint32_t*>(pixels_.data);for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x){const auto at=std::size_t(y)*1632+x;eraseMask_[at]=((source[at]^beforeErase_[at])&0x00ffffffu)?255:0;}return submitChecked(r,ContentMono,ModePen,2)?Submit::Accepted:Submit::Failed;}
// Classify the composed, vendor-aligned footprint without changing pixels or
// extent. These are pinned EPContentType values, not lower waveform numbers.
int VendorDisplay::qualityContent(Rect r)const noexcept{
 const auto footprint=conversionFootprint(r,pixels_.width,pixels_.height);
 const int left=footprint.x0,top=footprint.y0,right=footprint.x1,bottom=footprint.y1;
 const auto* source=reinterpret_cast<const std::uint32_t*>(pixels_.data);bool gray=false;
 for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){const auto p=source[std::size_t(y)*(pixels_.stride/4)+x];const auto red=(p>>16)&255u,green=(p>>8)&255u,blue=p&255u;if(red!=green||green!=blue)return ContentColor;if(red!=0&&red!=255)gray=true;}
 return gray?ContentGrayscale:ContentMono;
}
Submit VendorDisplay::clean(Rect r)noexcept{
 if(!ready(r))return Submit::Failed;
 return submitChecked(r,qualityContent(r),ModeUi,0)?Submit::Accepted:Submit::Failed;
}
// Erase-to-white retains the content class of the removed ink. The black
// palette pigment itself has unequal RGB channels; new white pixels alone
// must not discard that color transition.
Submit VendorDisplay::cleanRemoval(Rect r,int previousContent)noexcept{
 if(!ready(r)||(previousContent!=ContentMono&&previousContent!=ContentGrayscale&&previousContent!=ContentColor))return Submit::Failed;
 return submitChecked(r,std::max(previousContent,qualityContent(r)),ModeUi,0)?Submit::Accepted:Submit::Failed;
}
Submit VendorDisplay::ui(Rect r)noexcept{return clean(r);}
bool VendorDisplay::initialPreparedPage(std::span<const std::uint32_t> prepared)noexcept{
 Surface s=surface();if(!s.valid()||submissions_!=0||prepared.size()!=s.pixels.size())return false;
 if(prepared.data()!=s.pixels.data())std::copy(prepared.begin(),prepared.end(),s.pixels.begin());
 return submitChecked({0,0,s.width,s.height},ContentMono,ModeUi,1);
}
bool VendorDisplay::readerPage()noexcept{return submitChecked({0,0,pixels_.width,pixels_.height},ContentMono,ModeUi,1);}
void VendorDisplay::pump()noexcept{if(active_&&!retired_&&!inside_&&lease_->valid()&&!lease_->stopRequested()&&monotonic()<lease_->deadline()){inside_=true;calls_.events();inside_=false;}}
void VendorDisplay::shutdown()noexcept{active_=false;retired_=true;pixels_={};lease_=nullptr;}
}
