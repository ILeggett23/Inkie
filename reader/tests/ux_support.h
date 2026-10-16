#pragma once
#include "ux_shell.h"
#include <QJsonDocument>
#include <sys/stat.h>
#include <fstream>
#include <cstdio>
#include <source_location>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"CHECK_FAILED %s:%d: %s\n",__FILE__,__LINE__,#x);std::fflush(stderr);throw std::runtime_error(#x);}}while(0)
// A timeout names the caller's line (the CHECK inside would only name this header).
template<class F>void until(F f,std::source_location at=std::source_location::current()){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!f()){if(std::chrono::steady_clock::now()>=end)std::fprintf(stderr,"UNTIL_TIMEOUT %s:%u\n",at.file_name(),unsigned(at.line()));CHECK(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
inline std::filesystem::path stage(const std::filesystem::path& source,const std::filesystem::path& data,const std::filesystem::path& binaryDirectory,const std::filesystem::path& sys){
 auto app=data/"application";underlay::privateDirectory(app/"bin");underlay::privateDirectory(app/"fixtures");std::filesystem::create_directories(app/"assets");std::filesystem::copy(source/"assets/fonts",app/"assets/fonts",std::filesystem::copy_options::recursive);std::filesystem::copy_file(source/"fixtures/inkbound-device-test.pdf",app/"fixtures/inkbound-device-test.pdf");std::filesystem::copy_file(binaryDirectory/"reader-experience1-prepare",app/"bin/helper-real");
 auto wrapper=QByteArray("#!/bin/sh\nexec '")+(sys/"usr/lib/ld-linux-aarch64.so.1").string().c_str()+"' --library-path '"+(sys/"usr/lib").string().c_str()+"' '"+(app/"bin/helper-real").string().c_str()+"' \"$@\"\n";underlay::atomicFile(app/"bin/reader-experience1-prepare",wrapper);chmod((app/"bin/reader-experience1-prepare").c_str(),0700);return app;
}
struct Canvas:spike::Display {
 ux::Shell& shell;std::vector<std::uint32_t> pixels;unsigned submissions{},full{},cleanups{};spike::Rect cleaned{};std::uint64_t area{},version{};
 explicit Canvas(ux::Shell& s):shell(s),pixels(underlay::Pixels,0xffffffffu),version(s.version()){}
 spike::Surface surface(){return {pixels,underlay::Width,underlay::Height,underlay::Stride};}
 spike::Submit submit(spike::Rect r)noexcept override{try{r=shell.presentationDamage(r);shell.compose(surface(),r);shell.presentationCovered(r);++submissions;area+=std::uint64_t(r.x1-r.x0)*(r.y1-r.y0);return spike::Submit::Accepted;}catch(...){return spike::Submit::Failed;}}
 void changes(){auto change=shell.takeChange();if(!change.cleanup.empty()){++cleanups;cleaned=change.cleanup;}if(change.full){CHECK(submit({0,0,1620,2160})==spike::Submit::Accepted);++full;}else{for(unsigned i=0;i<ux::RegionCount;++i)if(change.regions&(1u<<i))CHECK(submit(ux::regions()[i])==spike::Submit::Accepted);for(unsigned i=0;i<change.extraCount;++i)CHECK(submit(change.extra[i])==spike::Submit::Accepted);}
  // Production session.cpp acknowledges a new pageVersion after the boundary,
  // including a viewport presented regionally. Full refresh is not the gate.
  if(change.full||version!=shell.version()){version=shell.version();shell.installed();}}
 void ready(){until([&]{shell.tick();changes();CHECK(!shell.failed());return shell.writable();});}
};
// Fixture setup for selection-action tests. Actual finger holds now open a
// definition; public Selection-tool pen/drag routing has separate Engine tests.
inline bool selectForActions(ux::Shell& shell,int x,int y){auto g=shell.semantics().geometry();if(!g)return false;const bool selected=shell.semantics().longPress(x,y,g->pageId());shell.tick();return selected;}
inline QByteArray inkHash(ux::Shell& s){auto pixels=s.visibleInk();return underlay::digest(QByteArray(reinterpret_cast<const char*>(pixels.data()),pixels.size_bytes())).toHex();}
inline void preview(const std::filesystem::path& path,std::span<const std::uint32_t> pixels){std::ofstream out(path,std::ios::binary);out<<"P5\n1620 2160\n255\n";for(int y=0;y<2160;++y)for(int x=0;x<1620;++x)out.put(char(pixels[y*1632+x]&255));}
#include "ui_targets.h"
#include <algorithm>
// Page overview through the real shell and thumbnail helper: the first screen of pages arrives as a regional refresh
// (no full-screen flash), every image is within the helper's bounds and shows real page content, and nothing is
// written under the document's storage.
inline void overviewThumbnails(ux::Shell& s,Canvas& c,const std::filesystem::path& docroot){
 auto listing=[&]{std::vector<std::string> names;for(const auto& e:std::filesystem::recursive_directory_iterator(docroot))names.push_back(e.path().string());std::sort(names.begin(),names.end());return names;};
 // The reader keeps preparing neighbouring pages after a layout change; start from a folder that has stopped changing.
 until([&]{auto a=listing();const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);while(std::chrono::steady_clock::now()<end){s.tick();c.changes();CHECK(!s.failed());if(s.document()->preparing())return false;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return listing()==a;});
 const auto before=listing();
 CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::openSettings(s));c.changes();CHECK(uitest::setting(s,ib::sheets::TabBase+2));c.changes();
 const int shown=std::min(s.document()->total(),ib::sheets::OverviewPageSize);const unsigned fulls=c.full;
 until([&]{s.tick();c.changes();CHECK(!s.failed()&&s.thumbnails().refusals()==0);for(int p=0;p<shown;++p)if(!s.thumbnails().find(p))return false;return true;});
 CHECK(c.full==fulls);
 for(int p=0;p<shown;++p){const auto* i=s.thumbnails().find(p);CHECK(i->page==p&&(i->width==thumbs::MaxWidth||i->height==thumbs::MaxHeight)&&i->gray.size()==std::size_t(i->width)*std::size_t(i->height));}
 const auto& first=s.thumbnails().find(0)->gray;const auto [lo,hi]=std::minmax_element(first.begin(),first.end());CHECK(*lo<128&&*hi>230);
 CHECK(uitest::setting(s,ib::sheets::Cancel));c.changes();{const auto after=listing();for(const auto& n:after)if(!std::binary_search(before.begin(),before.end(),n))std::fprintf(stderr,"OVERVIEW_ADDED %s\n",n.c_str());for(const auto& n:before)if(!std::binary_search(after.begin(),after.end(),n))std::fprintf(stderr,"OVERVIEW_REMOVED %s\n",n.c_str());CHECK(after==before);}
}
