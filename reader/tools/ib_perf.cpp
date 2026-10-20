// SPDX-License-Identifier: AGPL-3.0-only
// Host timing baseline for the presentation kit: paints every ib-preview surface (the same models and painters the
// tablet uses) and times the text layout helpers on long strings. Pure C++, no Qt, no display. Numbers are host CPU
// time: use them to compare two trees on one machine, not as tablet latencies (the device E2T markers measure those).
// usage: ib-perf <reader-dir> [iterations]
#define main ib_preview_main
#include "ib_preview.cpp"
#undef main
#include <algorithm>
#include <chrono>
int main(int argc,char** argv){
 if(argc<2||argc>3){std::fprintf(stderr,"usage: ib-perf <reader-dir> [iterations]\n");return 2;}
 const std::filesystem::path reader=argv[1];if(!text::load(reader/"assets/fonts")){std::fprintf(stderr,"fonts missing\n");return 2;}
 const int n=argc==3?std::max(1,std::atoi(argv[2])):20;
 using Clock=std::chrono::steady_clock;auto us=[](Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::micro>(b-a).count();};
 // Median of n runs after one warm-up run (caches filled), so a run is comparable across trees.
 auto median=[&](auto&& f){f();std::vector<double> t;t.reserve(std::size_t(n));for(int i=0;i<n;++i){auto a=Clock::now();f();t.push_back(us(a,Clock::now()));}std::sort(t.begin(),t.end());return t[t.size()/2];};
 double total=0;unsigned count=0;
 for(const auto& s:surfaces()){reader::setToolbarSide(reader::Side::Left);Image im;
  const double m=median([&]{auto c=im.canvas();Targets tg;s.paint(c,tg);});total+=m;++count;std::printf("IB_PERF surface=%s median_us=%.0f\n",s.name.c_str(),m);}
 const std::string title(240,'x'),words=[]{std::string w;for(int i=0;i<120;++i)w+="reading ";return w;}();
 const Font label{Face::Text,float(t::Label.size)},body{Face::Serif,float(t::Body.size)};
 std::printf("IB_PERF text=fit_240 median_us=%.1f\n",median([&]{(void)text::fit(label,title,600);}));
 std::printf("IB_PERF text=wrap_120_words median_us=%.1f\n",median([&]{(void)text::wrap(body,words,1200);}));
 std::printf("IB_PERF text=measure_960 median_us=%.1f\n",median([&]{(void)text::measure(body,words);}));
 std::printf("IB_PERF surfaces=%u total_median_us=%.0f iterations=%d\n",count,total,n);
 return 0;
}
