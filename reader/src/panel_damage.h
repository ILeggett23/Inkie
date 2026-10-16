// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "core.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <span>
namespace ux {
/* Fixed-memory tiled differences preserve separate old/new locations. Only the least costly pair is merged when the output is full. */
template<std::size_t N>
unsigned changedRegions(std::span<const std::uint32_t> before,std::span<const std::uint32_t> after,int width,int height,int stride,int top,std::array<spike::Rect,N>& out)noexcept{static_assert(N>0);if(width<=0||height<=0||stride<width||before.size()<std::size_t(stride)*height||after.size()<std::size_t(stride)*height)return 0;top=std::clamp(top,0,height);std::array<spike::Rect,N+1> regions{};unsigned count=0;auto area=[](spike::Rect r){return r.empty()?std::uint64_t(0):std::uint64_t(r.x1-r.x0)*std::uint64_t(r.y1-r.y0);};auto add=[&](spike::Rect rect){regions[count++]=rect;while(count>N){unsigned a=0,b=1;std::uint64_t best=UINT64_MAX;for(unsigned i=0;i<count;++i)for(unsigned j=i+1;j<count;++j){auto joined=regions[i];joined.include(regions[j]);const auto sum=area(regions[i])+area(regions[j]),cost=area(joined)>sum?area(joined)-sum:0;if(cost<best){best=cost;a=i;b=j;}}regions[a].include(regions[b]);regions[b]=regions[--count];}};constexpr int Tile=64;for(int y0=top;y0<height;y0+=Tile)for(int x0=0;x0<width;x0+=Tile){const int xEnd=std::min(width,x0+Tile),yEnd=std::min(height,y0+Tile);int left=xEnd,right=x0,upper=yEnd,lower=y0;for(int y=y0;y<yEnd;++y){const auto row=std::size_t(y)*stride;if(std::memcmp(before.data()+row+x0,after.data()+row+x0,std::size_t(xEnd-x0)*sizeof(std::uint32_t))==0)continue;upper=std::min(upper,y);lower=y+1;for(int x=x0;x<xEnd;++x)if(before[row+x]!=after[row+x]){left=std::min(left,x);right=std::max(right,x+1);}}if(right>left)add({std::max(0,left-4),std::max(top,upper-4),std::min(width,right+4),std::min(height,lower+4)});}for(unsigned i=0;i<count;++i)out[i]=regions[i];return count;}
}
