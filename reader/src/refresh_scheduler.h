// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "refresh_plan.h"
#include "refresh_policy.h"
#include "vendor.h"
#include <cstdint>
#ifdef INKBOUND_REFRESH_DEBUG
#include <string>
#endif
namespace ux {
enum class RefreshIntent:unsigned {FastInk,UIUpdate,ContentUpdate,PageTransition,Cleanup};
enum class RefreshMechanics:unsigned {FastRegion,QualityRegion,QualityPage};
constexpr RefreshMechanics mechanics(RefreshIntent intent)noexcept{return intent==RefreshIntent::PageTransition?RefreshMechanics::QualityPage:(intent==RefreshIntent::Cleanup||intent==RefreshIntent::UIUpdate)?RefreshMechanics::QualityRegion:RefreshMechanics::FastRegion;}
inline spike::Submit applyRefresh(spike::VendorDisplay& vendor,spike::Rect region,RefreshIntent intent,FullRefreshReason reason=FullRefreshReason::None)noexcept{switch(mechanics(intent)){case RefreshMechanics::QualityPage:return vendor.readerPage(reason)?spike::Submit::Accepted:spike::Submit::Failed;case RefreshMechanics::QualityRegion:return vendor.clean(region);case RefreshMechanics::FastRegion:return vendor.submit(region);}return spike::Submit::Failed;}
/* Existing vendor methods only. No private sync, new waveform or deadline on reading. Human contact always wins. */
class RefreshController {
 static constexpr unsigned Columns=5,Rows=6,Cells=Columns*Rows;unsigned limit_{96};std::array<unsigned,Cells> updates_{};std::array<spike::Rect,Cells> dirty_{};std::uint32_t pending_{};std::uint64_t retryAt_{};bool scenePending_{};FullRefreshReason sceneReason_{FullRefreshReason::None};
public:
 explicit RefreshController(unsigned partialLimit=96)noexcept:limit_(std::clamp(partialLimit,1u,4096u)){}
 // Retain the existing Inkbound limit as an estimate for all fast work. It is
 // not a measured ghost score or a reason to request a complete refresh.
 void requestScene(FullRefreshReason reason=FullRefreshReason::None)noexcept{scenePending_=true;if(completeRefreshAllowed(reason)&&unsigned(reason)>unsigned(sceneReason_))sceneReason_=reason;}
 SceneRefresh scene(RefreshConditions c)const noexcept{if(!scenePending_)return SceneRefresh::None;if(!refreshSettled(c))return SceneRefresh::Deferred;return completeRefreshAllowed(sceneReason_)?SceneRefresh::Complete:SceneRefresh::Regional;}
 FullRefreshReason sceneReason()const noexcept{return sceneReason_;}
 void sceneComposed()noexcept{scenePending_=false;sceneReason_=FullRefreshReason::None;}
 struct Feedback {RefreshIntent intent;bool cleanupDebt{},defer{};};
 static Feedback feedback(bool content,bool preview,bool generated,RefreshConditions c)noexcept{if(generated)return {RefreshIntent::FastInk};if(content)return {RefreshIntent::ContentUpdate};if(preview||refreshSettled(c))return {RefreshIntent::UIUpdate};return {RefreshIntent::FastInk,true,!c.pen&&!c.erasing};}
 bool qualityAllowed(RefreshConditions c,std::uint64_t now)const noexcept{return refreshSettled(c)&&now>=retryAt_;}
 std::array<std::uint64_t,5> counts{},areas{};
 void accepted(spike::Rect r,RefreshIntent intent,bool generated=false)noexcept{if(r.empty())return;const auto index=unsigned(intent);if(counts[index]!=UINT64_MAX)++counts[index];areas[index]=RefreshPlan::area(r)>UINT64_MAX-areas[index]?UINT64_MAX:areas[index]+RefreshPlan::area(r);if(intent==RefreshIntent::PageTransition){sceneComposed();updates_={};dirty_={};pending_=0;retryAt_=0;return;}if(intent==RefreshIntent::Cleanup||intent==RefreshIntent::UIUpdate){for(unsigned i=0;i<Cells;++i){auto d=dirty_[i];if(!d.empty()&&r.x0<=d.x0&&r.y0<=d.y0&&r.x1>=d.x1&&r.y1>=d.y1){updates_[i]=0;dirty_[i]={};pending_&=~(1u<<i);}}return;}(void)generated;if(intent!=RefreshIntent::FastInk&&intent!=RefreshIntent::ContentUpdate)return;for(unsigned y=0;y<Rows;++y)for(unsigned x=0;x<Columns;++x){spike::Rect cell{int(x*1620/Columns),int(y*2160/Rows),int((x+1)*1620/Columns),int((y+1)*2160/Rows)};spike::Rect part{std::max(r.x0,cell.x0),std::max(r.y0,cell.y0),std::min(r.x1,cell.x1),std::min(r.y1,cell.y1)};if(part.empty())continue;const auto i=y*Columns+x;if(updates_[i]<limit_)++updates_[i];dirty_[i].include(part);if(updates_[i]>=limit_)pending_|=1u<<i;}}
 RefreshPlan cleanup(RefreshConditions c,std::uint64_t now)const noexcept{RefreshPlan plan;if(!pending_||now<retryAt_||c.pen||c.erasing||c.saving||(c.animating&&!c.quiet))return plan;spike::Rect all{};for(unsigned i=0;i<Cells;++i)if(pending_&(1u<<i)){all.include(dirty_[i]);(void)plan.add(dirty_[i]);}if(plan.overflow){plan={};(void)plan.add(all);}return plan;}
 void failed(std::uint64_t now)noexcept{retryAt_=now>UINT64_MAX-500000000ull?UINT64_MAX:now+500000000ull;}
 bool pending()const noexcept{return pending_!=0;}bool ready(std::uint64_t now)const noexcept{return now>=retryAt_;}
};
#ifdef INKBOUND_REFRESH_DEBUG
/* Offline development visualization; no debug pixels or file writes in production/contact paths. */
inline std::string refreshDebugSvg(const RefreshPlan& plan){std::string out="<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1620 2160\"><rect width=\"1620\" height=\"2160\" fill=\"white\"/>";for(unsigned i=0;i<plan.count;++i){const auto r=plan.regions[i];out+="<rect x=\""+std::to_string(r.x0)+"\" y=\""+std::to_string(r.y0)+"\" width=\""+std::to_string(r.x1-r.x0)+"\" height=\""+std::to_string(r.y1-r.y0)+"\" fill=\"none\" stroke=\"#bd5250\" stroke-width=\"3\"/>";}return out+"</svg>";}
#endif
}
