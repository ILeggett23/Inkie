#pragma once
#include "ux_shell.h"
#include <QJsonDocument>
#include <sys/stat.h>
#include <fstream>
#include <cstdio>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
template<class F>void until(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!f()){CHECK(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
inline std::filesystem::path stage(const std::filesystem::path& source,const std::filesystem::path& data,const std::filesystem::path& binaryDirectory,const std::filesystem::path& sys){
 auto app=data/"application";underlay::privateDirectory(app/"bin");underlay::privateDirectory(app/"fixtures");std::filesystem::copy_file(source/"fixtures/inkbound-device-test.pdf",app/"fixtures/inkbound-device-test.pdf");std::filesystem::copy_file(binaryDirectory/"reader-experience1-prepare",app/"bin/helper-real");
 auto wrapper=QByteArray("#!/bin/sh\nexec '")+(sys/"usr/lib/ld-linux-aarch64.so.1").string().c_str()+"' --library-path '"+(sys/"usr/lib").string().c_str()+"' '"+(app/"bin/helper-real").string().c_str()+"' \"$@\"\n";underlay::atomicFile(app/"bin/reader-experience1-prepare",wrapper);chmod((app/"bin/reader-experience1-prepare").c_str(),0700);return app;
}
struct Canvas:spike::Display {
 ux::Shell& shell;std::vector<std::uint32_t> pixels;unsigned submissions{},full{};std::uint64_t area{};
 explicit Canvas(ux::Shell& s):shell(s),pixels(underlay::Pixels,0xffffffffu){}
 spike::Surface surface(){return {pixels,underlay::Width,underlay::Height,underlay::Stride};}
 spike::Submit submit(spike::Rect r)noexcept override{try{underlay::compose(surface(),shell.background(),shell.ink(),r);++submissions;area+=std::uint64_t(r.x1-r.x0)*(r.y1-r.y0);return spike::Submit::Accepted;}catch(...){return spike::Submit::Failed;}}
 void changes(){auto change=shell.takeChange();if(change.full){CHECK(submit({0,0,1620,2160})==spike::Submit::Accepted);++full;shell.installed();}else for(unsigned i=0;i<ux::Regions.size();++i)if(change.regions&(1u<<i))CHECK(submit(ux::Regions[i])==spike::Submit::Accepted);}
 void ready(){until([&]{shell.tick();changes();CHECK(!shell.failed());return shell.writable();});}
};
inline QByteArray inkHash(ux::Shell& s){auto pixels=s.ink().pixels;return underlay::digest(QByteArray(reinterpret_cast<const char*>(pixels.data()),pixels.size_bytes())).toHex();}
inline void preview(const std::filesystem::path& path,std::span<const std::uint32_t> pixels){std::ofstream out(path,std::ios::binary);out<<"P5\n1620 2160\n255\n";for(int y=0;y<2160;++y)for(int x=0;x<1620;++x)out.put(char(pixels[y*1632+x]&255));}
