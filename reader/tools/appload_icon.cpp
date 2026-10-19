// SPDX-License-Identifier: AGPL-3.0-only
// The Inkie launcher icon for AppLoad (icon.png, 256x256 RGB): a fountain-pen nib standing on its tip, whose slit is the
// stem of a lowercase "i", with the wordmark's magenta drop of ink as its dot (docs/INKIE-DESIGN.md section 5), on a
// paper tile closed by an ink rule. Drawn with the ib kit at 4x and box-filtered down, so the edges stay clean on the
// launcher. Usage: appload-icon <out.png>
#include "ib/canvas.h"
#include "ib/theme.h"
#include <zlib.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace {
using namespace ib;
namespace t=theme;
constexpr int Out=256,Scale=4,S=Out*Scale;
void be32(std::string& s,std::uint32_t v){for(int i=3;i>=0;--i)s.push_back(char(v>>(i*8)&255));}
void chunk(std::ofstream& out,const char* type,const std::string& data){std::string c(type,4);c+=data;std::string len;be32(len,std::uint32_t(data.size()));std::string crc;be32(crc,std::uint32_t(crc32(0,reinterpret_cast<const Bytef*>(c.data()),uInt(c.size()))));out<<len<<c<<crc;}
bool png(const std::filesystem::path& path,const std::vector<std::uint32_t>& px,int w,int h){
 std::string raw;raw.reserve(std::size_t(h)*(w*3+1));
 for(int y=0;y<h;++y){raw.push_back(0);for(int x=0;x<w;++x){auto p=px[std::size_t(y)*w+x];raw.push_back(char(p>>16&255));raw.push_back(char(p>>8&255));raw.push_back(char(p&255));}}
 uLongf n=compressBound(uLong(raw.size()));std::string z(n,'\0');if(compress2(reinterpret_cast<Bytef*>(z.data()),&n,reinterpret_cast<const Bytef*>(raw.data()),uLong(raw.size()),9)!=Z_OK)return false;z.resize(n);
 std::ofstream out(path,std::ios::binary);out<<"\x89PNG\r\n\x1a\n";std::string ihdr;be32(ihdr,std::uint32_t(w));be32(ihdr,std::uint32_t(h));ihdr+=std::string("\x08\x02\x00\x00\x00",5);
 chunk(out,"IHDR",ihdr);chunk(out,"IDAT",z);chunk(out,"IEND","");return bool(out);}
}
int main(int argc,char** argv){
 if(argc!=2){std::fprintf(stderr,"usage: appload-icon <out.png>\n");return 2;}
 std::vector<std::uint32_t> big(std::size_t(S)*S,0xffffffffu);Canvas c({big,S,S,S});
 const float u=S/1024.f;auto P=[&](float v){return v*u;};
 // Tile: paper, closed by an ink rule.
 c.fillRound({int(P(36)),int(P(36)),int(P(988)),int(P(988))},P(200),t::Paper);
 c.strokeRound({int(P(36)),int(P(36)),int(P(988)),int(P(988))},P(200),P(22),t::InkPrimary);
 // Nib: shoulders at the top, tapering to the writing point; ink outline over paper.
 Path nib;nib.move(P(318),P(452)).cubic(P(318),P(610),P(420),P(770),P(512),P(888)).cubic(P(604),P(770),P(706),P(610),P(706),P(452))
  .quad(P(512),P(404),P(318),P(452)).close();
 c.fillPath(nib,t::Paper);c.stroke(nib,P(30),t::InkPrimary);
 // Shoulder imprint, a little below the top edge.
 Path shoulder;shoulder.move(P(352),P(522)).quad(P(512),P(478),P(672),P(522));c.stroke(shoulder,P(16),t::InkPrimary);
 // The stem of the i: the slit from the point up to the breather hole.
 c.line(P(512),P(860),P(512),P(668),P(26),t::InkPrimary);c.fillCircle(P(512),P(640),P(36),t::InkPrimary);c.fillCircle(P(512),P(640),P(14),t::Paper);
 // The dot of the i: a magenta drop of ink, point up.
 Path drop;drop.move(P(512),P(150)).cubic(P(548),P(214),P(592),P(256),P(592),P(304)).cubic(P(592),P(352),P(556),P(384),P(512),P(384))
  .cubic(P(468),P(384),P(432),P(352),P(432),P(304)).cubic(P(432),P(256),P(476),P(214),P(512),P(150)).close();
 c.fillPath(drop,t::PopMagenta);c.stroke(drop,P(14),t::InkPrimary);
 // Box filter to 256 px.
 std::vector<std::uint32_t> px(std::size_t(Out)*Out);
 for(int y=0;y<Out;++y)for(int x=0;x<Out;++x){unsigned r=0,g=0,b=0;for(int j=0;j<Scale;++j)for(int i=0;i<Scale;++i){auto p=big[std::size_t(y*Scale+j)*S+x*Scale+i];r+=p>>16&255;g+=p>>8&255;b+=p&255;}
  const unsigned n=Scale*Scale;px[std::size_t(y)*Out+x]=0xff000000u|((r/n)<<16)|((g/n)<<8)|(b/n);}
 if(!png(argv[1],px,Out,Out)){std::fprintf(stderr,"write failed\n");return 1;}
 std::printf("APPLOAD_ICON written=%s size=%dx%d\n",argv[1],Out,Out);return 0;}
