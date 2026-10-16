#pragma once
#include "core.h"
#include <array>
#include <algorithm>
namespace ux {
/* Fixed memory; merge only when one rectangle costs no more pixel area than the prior two submissions. */
struct RefreshPlan {
 std::array<spike::Rect,16> regions{};unsigned count{};bool overflow{};
 static std::uint64_t area(spike::Rect r)noexcept{return r.empty()?0:std::uint64_t(r.x1-r.x0)*std::uint64_t(r.y1-r.y0);}
 bool subtract(spike::Rect cut)noexcept{RefreshPlan next;auto keep=[&](spike::Rect r){if(r.empty())return true;if(next.count==next.regions.size())return false;next.regions[next.count++]=r;return true;};for(unsigned i=0;i<count;++i){const auto r=regions[i];const spike::Rect hit{std::max(r.x0,cut.x0),std::max(r.y0,cut.y0),std::min(r.x1,cut.x1),std::min(r.y1,cut.y1)};if(hit.empty()){if(!keep(r))return false;}else if(!keep({r.x0,r.y0,r.x1,hit.y0})||!keep({r.x0,hit.y1,r.x1,r.y1})||!keep({r.x0,hit.y0,hit.x0,hit.y1})||!keep({hit.x1,hit.y0,r.x1,hit.y1}))return false;}*this=next;return true;}
 /* a minus b as one rectangle when b covers a full edge band of a; false when nothing or two or more pieces would remain */
 static bool remainder(spike::Rect a,spike::Rect b,spike::Rect& out)noexcept{const spike::Rect hit{std::max(a.x0,b.x0),std::max(a.y0,b.y0),std::min(a.x1,b.x1),std::min(a.y1,b.y1)};if(hit.empty())return false;const bool wide=hit.x0==a.x0&&hit.x1==a.x1,tall=hit.y0==a.y0&&hit.y1==a.y1;if(wide&&hit.y0==a.y0&&hit.y1<a.y1){out={a.x0,hit.y1,a.x1,a.y1};return true;}if(wide&&hit.y1==a.y1&&hit.y0>a.y0){out={a.x0,a.y0,a.x1,hit.y0};return true;}if(tall&&hit.x0==a.x0&&hit.x1<a.x1){out={hit.x1,a.y0,a.x1,a.y1};return true;}if(tall&&hit.x1==a.x1&&hit.x0>a.x0){out={a.x0,a.y0,hit.x0,a.y1};return true;}return false;}
 /* Never more regions than inputs and never more area than their sum. A band overlap is trimmed so its pixels go out once;
    a crossing overlap, whose difference needs two or more pieces, stays as two regions rather than growing the submission count. */
 bool add(spike::Rect r)noexcept{r={std::clamp(r.x0,0,1620),std::clamp(r.y0,0,2160),std::clamp(r.x1,0,1620),std::clamp(r.y1,0,2160)};if(r.empty())return true;for(unsigned i=0;i<count;){auto q=regions[i];if(q.x0<=r.x0&&q.y0<=r.y0&&q.x1>=r.x1&&q.y1>=r.y1)return true;auto joined=r;joined.include(q);const bool overlap=std::max(q.x0,r.x0)<std::min(q.x1,r.x1)&&std::max(q.y0,r.y0)<std::min(q.y1,r.y1);if(!overlap){++i;continue;}spike::Rect piece;if(area(joined)<=area(q)+area(r)){r=joined;regions[i]=regions[--count];i=0;}else if(remainder(r,q,piece)){r=piece;i=0;}else if(remainder(q,r,piece)){regions[i]=piece;i=0;}else ++i;}if(count==regions.size()){overflow=true;return false;}regions[count++]=r;return true;}
};
}
