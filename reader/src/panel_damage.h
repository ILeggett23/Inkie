// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "core.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <span>
namespace ux {
// Bounded UI queue: retain every input and merge the least expensive pair only
// at capacity. A large region is damage, not permission for a complete refresh.
template<std::size_t N>
void accumulateDamage(std::array<spike::Rect,N>& out,unsigned& count,spike::Rect rect)noexcept{
 static_assert(N>0);
 if(rect.empty())return;
 auto covers=[](spike::Rect a,spike::Rect b){return a.x0<=b.x0&&a.y0<=b.y0&&a.x1>=b.x1&&a.y1>=b.y1;};
 for(unsigned i=0;i<count;++i)if(covers(out[i],rect))return;
 unsigned keep=0;for(unsigned i=0;i<count;++i)if(!covers(rect,out[i]))out[keep++]=out[i];count=keep;
 if(count<N){out[count++]=rect;return;}
 std::array<spike::Rect,N+1> regions{};std::copy_n(out.begin(),count,regions.begin());regions[count]=rect;
 auto area=[](spike::Rect r){return r.empty()?std::uint64_t(0):std::uint64_t(r.x1-r.x0)*std::uint64_t(r.y1-r.y0);};
 unsigned a=0,b=1;std::uint64_t best=UINT64_MAX;
 for(unsigned i=0;i<=count;++i)for(unsigned j=i+1;j<=count;++j){auto joined=regions[i];joined.include(regions[j]);const auto sum=area(regions[i])+area(regions[j]),cost=area(joined)>sum?area(joined)-sum:0;if(cost<best){best=cost;a=i;b=j;}}
 regions[a].include(regions[b]);regions[b]=regions[count];std::copy_n(regions.begin(),count,out.begin());
}
/* Fixed-memory tiled differences preserve separate old/new locations. Only the least costly pair is merged when the output is full. */
template<std::size_t N>
unsigned changedRegions(std::span<const std::uint32_t> before,std::span<const std::uint32_t> after,int width,int height,int stride,int top,std::array<spike::Rect,N>& out)noexcept{static_assert(N>0);if(width<=0||height<=0||stride<width||before.size()<std::size_t(stride)*height||after.size()<std::size_t(stride)*height)return 0;top=std::clamp(top,0,height);unsigned count=0;auto add=[&](spike::Rect rect){accumulateDamage(out,count,rect);};constexpr int Tile=64;for(int y0=top;y0<height;y0+=Tile)for(int x0=0;x0<width;x0+=Tile){const int xEnd=std::min(width,x0+Tile),yEnd=std::min(height,y0+Tile);int left=xEnd,right=x0,upper=yEnd,lower=y0;for(int y=y0;y<yEnd;++y){const auto row=std::size_t(y)*stride;if(std::memcmp(before.data()+row+x0,after.data()+row+x0,std::size_t(xEnd-x0)*sizeof(std::uint32_t))==0)continue;upper=std::min(upper,y);lower=y+1;for(int x=x0;x<xEnd;++x)if(before[row+x]!=after[row+x]){left=std::min(left,x);right=std::max(right,x+1);}}if(right>left)add({std::max(0,left-4),std::max(top,upper-4),std::min(width,right+4),std::min(height,lower+4)});}return count;}

// Compare canonical scene replacements with the last software-submitted pixels.
// Reuse one 64-row strip, rather than allocating another full-frame image.
// compose must respect its region, as required by ordinary regional rendering.
// The tile size/padding match changedRegions; neither is a stock cleanup law.
template<std::size_t N,class Compose>
bool recomposeDamage(spike::Surface surface,std::span<std::uint32_t> rows,
                     Compose&& compose,std::array<spike::Rect,N>& out,unsigned& count){
 static_assert(N>0);count=0;constexpr int Tile=64;
 if(!surface.valid()||rows.size()<std::size_t(surface.width)*Tile)return false;
 for(int y0=0;y0<surface.height;y0+=Tile){
  const int yEnd=std::min(surface.height,y0+Tile);
  for(int y=y0;y<yEnd;++y)std::copy_n(surface.pixels.data()+std::size_t(y)*surface.stridePixels,surface.width,rows.data()+std::size_t(y-y0)*surface.width);
  compose(spike::Rect{0,y0,surface.width,yEnd});
  for(int x0=0;x0<surface.width;x0+=Tile){
   const int xEnd=std::min(surface.width,x0+Tile);int left=xEnd,right=x0,upper=yEnd,lower=y0;
   for(int y=y0;y<yEnd;++y){
    const auto* before=rows.data()+std::size_t(y-y0)*surface.width;
    const auto* after=surface.pixels.data()+std::size_t(y)*surface.stridePixels;
    if(std::memcmp(before+x0,after+x0,std::size_t(xEnd-x0)*sizeof(std::uint32_t))==0)continue;
    upper=std::min(upper,y);lower=y+1;
    for(int x=x0;x<xEnd;++x)if(before[x]!=after[x]){left=std::min(left,x);right=std::max(right,x+1);}
   }
   if(right>left)accumulateDamage(out,count,{std::max(0,left-4),std::max(0,upper-4),std::min(surface.width,right+4),std::min(surface.height,lower+4)});
  }
 }
 return true;
}
}
