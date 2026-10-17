#include <cctype>
// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/text.h"
// Vendored stb_truetype (public domain / MIT); only part of its API is used here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"
#pragma GCC diagnostic pop
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_map>
namespace ib::text {
namespace {
constexpr std::size_t MaxFontBytes=4*1024*1024,MaxGlyphs=6000;
constexpr std::array<const char*,std::size_t(Face::Count)> Files{
 "SourceSerif4-Regular.ttf","SourceSerif4-It.ttf","SourceSerif4-Semibold.ttf",
 // UI faces (Text*) are Source Sans 3, as on the native Paper Pro; book and quote text stay serif.
 // No Source Sans italic ships, so TextItalic keeps the small-text serif italic.
 "SourceSans3-Regular.ttf","SourceSerif4SmText-It.ttf","SourceSans3-Semibold.ttf",
 "SourceSerif4Subhead-Regular.ttf","SourceSerif4Display-Regular.ttf",
 "SourceSans3-Regular.ttf","SourceSans3-Medium.ttf","SourceSans3-Semibold.ttf","Caveat.ttf","Caveat.ttf","Caveat.ttf",
 "SourceSans3-Regular.ttf"};
struct Loaded {std::vector<unsigned char> bytes;stbtt_fontinfo info{};bool ok{};int ascent{},descent{},gap{};};
struct Glyph {std::vector<std::uint8_t> alpha;int w{},h{},x{},y{};};
struct Key {unsigned face;unsigned size;char32_t cp;unsigned sub;bool operator==(const Key&)const=default;};
struct KeyHash {std::size_t operator()(const Key& k)const noexcept{return (std::size_t(k.face)*1000003u)^(std::size_t(k.size)*7919u)^(std::size_t(k.cp)*31u)^k.sub;}};
struct State {
 std::array<Loaded,std::size_t(Face::Count)> fonts;std::unordered_map<Key,std::shared_ptr<const Glyph>,KeyHash> cache;std::mutex lock;float gain{1.12f};bool ready{};bool plain{};
};
State& state(){static State s;return s;}
bool readFile(const std::filesystem::path& path,std::vector<unsigned char>& out){
 std::unique_ptr<FILE,int(*)(FILE*)> f(std::fopen(path.c_str(),"rb"),std::fclose);if(!f)return false;
 if(std::fseek(f.get(),0,SEEK_END))return false;long n=std::ftell(f.get());if(n<=0||std::size_t(n)>MaxFontBytes||std::fseek(f.get(),0,SEEK_SET))return false;
 out.resize(std::size_t(n));return std::fread(out.data(),1,out.size(),f.get())==out.size();
}
Face fallbackFor(Face f){switch(f){case Face::Literal:return Face::Sans;case Face::Hand:case Face::Script:case Face::ScriptBold:return Face::SerifItalic;case Face::Display:case Face::Subhead:return Face::Serif;case Face::TextItalic:return Face::SerifItalic;case Face::TextSemibold:return Face::SerifSemibold;case Face::Text:return Face::Serif;default:return f;}}
Loaded& fontFor(Face f){auto& s=state();auto& l=s.fonts[unsigned(f)];if(l.ok)return l;auto& b=s.fonts[unsigned(fallbackFor(f))];if(b.ok)return b;return s.fonts[unsigned(Face::Serif)];}
// Decodes one code point; invalid sequences become U+FFFD and advance one byte.
char32_t next(std::string_view s,std::size_t& i){
 auto b=[&](std::size_t k){return static_cast<unsigned char>(s[k]);};unsigned char c=b(i);
 auto cont=[&](std::size_t k){return k<s.size()&&(b(k)&0xC0)==0x80;};
 if(c<0x80){++i;return c;}
 if((c&0xE0)==0xC0&&cont(i+1)){char32_t v=char32_t(c&0x1F)<<6|(b(i+1)&0x3F);i+=2;return v>=0x80?v:0xFFFD;}
 if((c&0xF0)==0xE0&&cont(i+1)&&cont(i+2)){char32_t v=char32_t(c&0x0F)<<12|char32_t(b(i+1)&0x3F)<<6|(b(i+2)&0x3F);i+=3;return v>=0x800&&(v<0xD800||v>0xDFFF)?v:0xFFFD;}
 if((c&0xF8)==0xF0&&cont(i+1)&&cont(i+2)&&cont(i+3)){char32_t v=char32_t(c&0x07)<<18|char32_t(b(i+1)&0x3F)<<12|char32_t(b(i+2)&0x3F)<<6|(b(i+3)&0x3F);i+=4;return v>=0x10000&&v<=0x10FFFF?v:0xFFFD;}
 ++i;return 0xFFFD;
}
// Picks the face that actually carries the code point (Serif/Sans/Text fall back to each other).
Loaded* carrier(Face f,char32_t cp,int& glyph){
 for(Face candidate:{f,fallbackFor(f),Face::Serif,Face::Sans}){auto& l=fontFor(candidate);if(!l.ok)continue;glyph=stbtt_FindGlyphIndex(&l.info,int(cp));if(glyph)return &l;}
 glyph=0;auto& l=fontFor(f);return l.ok?&l:nullptr;
}
float scale(const Loaded& l,float size){return stbtt_ScaleForMappingEmToPixels(&l.info,size);}
std::shared_ptr<const Glyph> raster(Loaded& l,unsigned faceKey,float size,char32_t cp,int glyph,unsigned sub){
 auto& s=state();Key key{faceKey,unsigned(std::lround(size*4)),cp,sub};
 {std::lock_guard g(s.lock);auto found=s.cache.find(key);if(found!=s.cache.end())return found->second;}
 auto out=std::make_shared<Glyph>();float sc=scale(l,size);int x0,y0,x1,y1;
 stbtt_GetGlyphBitmapBoxSubpixel(&l.info,glyph,sc,sc,sub/4.f,0,&x0,&y0,&x1,&y1);
 out->w=std::max(0,x1-x0);out->h=std::max(0,y1-y0);out->x=x0;out->y=y0;
 if(out->w>0&&out->h>0&&out->w<1024&&out->h<1024){out->alpha.resize(std::size_t(out->w)*out->h);stbtt_MakeGlyphBitmapSubpixel(&l.info,out->alpha.data(),out->w,out->h,out->w,sc,sc,sub/4.f,0,glyph);
  // ScriptBold: a heavier pen. Coverage spreads right by about a 26th of the size and down by half that, so strokes
  // thicken without the letters drifting from their advance.
  if(faceKey==unsigned(Face::ScriptBold)){const int dx=std::max(1,int(std::lround(size/26))),dy=std::max(1,dx/2),w=out->w+dx,h=out->h+dy;std::vector<std::uint8_t> bold(std::size_t(w)*h,0);
   for(int y=0;y<h;++y)for(int x=0;x<w;++x){std::uint8_t v=0;for(int j=0;j<=dy;++j){const int sy=y-j;if(sy<0||sy>=out->h)continue;for(int i=0;i<=dx;++i){const int sx=x-i;if(sx>=0&&sx<out->w)v=std::max(v,out->alpha[std::size_t(sy)*out->w+sx]);}}bold[std::size_t(y)*w+x]=v;}
   out->alpha=std::move(bold);out->w=w;out->h=h;}}
 else out->w=out->h=0;
 std::lock_guard g(s.lock);if(s.cache.size()>=MaxGlyphs)s.cache.clear();s.cache.emplace(key,out);return out;
}
struct Run {Loaded* font;int glyph;char32_t cp;float size;};
// font is already resolved. A code point Caveat lacks falls back to a plain face at the role size, not the cursive size.
template<class F>float walk(Font font,std::string_view str,F&& visit){
 float x=0;int previous=0;Loaded* previousFont=nullptr;const Loaded* script=font.face==Face::Script||font.face==Face::ScriptBold?&state().fonts[unsigned(font.face)]:nullptr;
 for(std::size_t i=0;i<str.size();){char32_t cp=next(str,i);if(cp=='\n'||cp=='\r'||cp=='\t')cp=' ';int glyph=0;Loaded* l=carrier(font.face,cp,glyph);if(!l)continue;
  const float size=script&&l!=script?font.size/ScriptScale:font.size;
  float sc=scale(*l,size);if(previous&&previousFont==l)x+=sc*stbtt_GetGlyphKernAdvance(&l->info,previous,glyph);
  int advance,bearing;stbtt_GetGlyphHMetrics(&l->info,glyph,&advance,&bearing);visit(Run{l,glyph,cp,size},x);x+=sc*advance+font.tracking;previous=glyph;previousFont=l;}
 return x;
}
}
bool load(const std::filesystem::path& dir,const std::filesystem::path& fixtureDir){
 auto& s=state();std::lock_guard g(s.lock);s.cache.clear();
 for(unsigned f=0;f<unsigned(Face::Count);++f){auto& l=s.fonts[f];if(l.ok)continue;auto base=dir;
  // Script and ScriptBold are the same Caveat file as Hand: share its bytes (the font info points into Hand's buffer).
  // Literal shares Sans the same way.
  if(const Face share=Face(f)==Face::Script||Face(f)==Face::ScriptBold?Face::Hand:Face(f)==Face::Literal?Face::Sans:Face::Count;share!=Face::Count&&s.fonts[unsigned(share)].ok){
   const auto& from=s.fonts[unsigned(share)];l.info=from.info;l.ascent=from.ascent;l.descent=from.descent;l.gap=from.gap;l.ok=true;continue;} // Hand (Caveat, Tom's handwriting) is bundled; previews may also supply it as a fixture
  if((Face(f)==Face::Hand||Face(f)==Face::Script||Face(f)==Face::ScriptBold)&&!std::filesystem::exists(dir/"Caveat.ttf"))base=fixtureDir;if(base.empty())continue;
  if(!readFile(base/Files[f],l.bytes))continue;int offset=stbtt_GetFontOffsetForIndex(l.bytes.data(),0);
  if(offset<0||!stbtt_InitFont(&l.info,l.bytes.data(),offset)){l.bytes.clear();continue;}
  stbtt_GetFontVMetrics(&l.info,&l.ascent,&l.descent,&l.gap);l.ok=true;}
 s.ready=s.fonts[unsigned(Face::Serif)].ok&&s.fonts[unsigned(Face::Text)].ok;return s.ready;
}
bool ready()noexcept{return state().ready;}
void setGain(float g)noexcept{state().gain=std::clamp(g,0.5f,2.f);}
void setPlainScript(bool on)noexcept{state().plain=on;}
bool plainScript()noexcept{return state().plain;}
// Resolved once at each leaf (measure, metrics, draw); composite helpers pass the role font through unchanged.
namespace {Font resolvedAs(Font f,bool plain)noexcept;}
Font resolved(Font f)noexcept{return resolvedAs(f,state().plain);}
namespace {Font resolvedAs(Font f,bool plain)noexcept{
 auto& s=state();const bool cursive=!plain&&s.fonts[unsigned(Face::Script)].ok;
 Face to=Face::Count;
 switch(f.face){
 case Face::Script:if(!cursive)return {f.plain,f.size,f.tracking,f.plain};to=Face::Script;break;
 case Face::Text:case Face::TextItalic:case Face::Sans:case Face::Serif:case Face::Subhead:case Face::Display:to=Face::Script;break;
 case Face::TextSemibold:case Face::SansMedium:case Face::SansSemibold:case Face::SerifSemibold:to=Face::ScriptBold;break;
 default:return f; // SerifItalic (the book's words), Literal (exact characters), Hand (Tom's pen source)
 }
 if(!cursive)return f;
 return {to,std::max(f.size*ScriptScale,ScriptFloor),f.tracking,f.face==Face::Script?f.plain:f.face};}}
float span(Font f,std::string_view s){if(!ready())return measure(f,s);return std::max(walk(resolvedAs(f,false),s,[](Run,float){}),walk(resolvedAs(f,true),s,[](Run,float){}));}
float ascent(Font f){f=resolved(f);auto& l=fontFor(f.face);return l.ok?l.ascent*scale(l,f.size):f.size*.8f;}
float descent(Font f){f=resolved(f);auto& l=fontFor(f.face);return l.ok?-l.descent*scale(l,f.size):f.size*.2f;}
float capHeight(Font f){f=resolved(f);auto& l=fontFor(f.face);if(!l.ok)return f.size*.66f;int x0,y0,x1,y1;if(!stbtt_GetCodepointBox(&l.info,'H',&x0,&y0,&x1,&y1))return f.size*.66f;return y1*scale(l,f.size);}
// Without fonts (never on a packaged device) keep layout stable with an approximate advance.
float measure(Font f,std::string_view s){f=resolved(f);if(!ready()){float n=0;for(std::size_t i=0;i<s.size();){next(s,i);n+=1;}return n*f.size*0.5f;}return walk(f,s,[](Run,float){});}
float draw(Canvas& c,Font f,float x,float baseline,std::string_view s,std::uint32_t color){
 if(!ready())return 0;f=resolved(f);float gain=state().gain;
 return walk(f,s,[&](Run r,float at){if(r.cp==' ')return;float px=x+at;int ix=int(std::floor(px));unsigned sub=unsigned((px-ix)*4)&3;
  auto g=raster(*r.font,unsigned(r.font-&state().fonts[0]),r.size,r.cp,r.glyph,sub);if(g->w)c.mask(ix+g->x,int(std::lround(baseline))+g->y,g->alpha.data(),g->w,g->h,g->w,color,gain);});
}
float drawAligned(Canvas& c,Font f,Rect box,float baseline,std::string_view s,std::uint32_t color,Align a){
 float w=measure(f,s);float x=a==Align::Left?box.x0:a==Align::Right?box.x1-w:box.x0+(width(box)-w)/2;return draw(c,f,x,baseline,s,color);
}
std::string fit(Font f,std::string_view s,float width){
 if(measure(f,s)<=width)return std::string(s);
 const std::string_view dots="\xE2\x80\xA6";float limit=width-measure(f,dots);std::size_t keep=0;
 for(std::size_t i=0;i<s.size();){next(s,i);if(measure(f,s.substr(0,i))>limit)break;keep=i;}
 std::string out(s.substr(0,keep));while(!out.empty()&&out.back()==' ')out.pop_back();return out+std::string(dots);
}
std::vector<std::string> wrap(Font f,std::string_view s,float width,int maxLines){
 std::vector<std::string> lines;if(maxLines<1)return lines;bool truncated=false;std::size_t para=0;
 while(true){
  std::size_t end=s.find('\n',para);if(end==std::string_view::npos)end=s.size();
  std::string_view p=s.substr(para,end-para);std::string line;std::size_t i=0;
  while(i<p.size()){
   while(i<p.size()&&p[i]==' ')++i;if(i>=p.size())break;
   std::size_t j=p.find(' ',i);if(j==std::string_view::npos)j=p.size();
   std::string word(p.substr(i,j-i));std::string candidate=line.empty()?word:line+" "+word;
   if(measure(f,candidate)<=width){line=std::move(candidate);i=j;continue;}
   if(int(lines.size())+1>=maxLines){truncated=true;break;}
   if(!line.empty()){lines.push_back(std::move(line));line.clear();continue;}
   // A single word wider than the line breaks by code point.
   std::size_t k=0,cut=0;while(k<word.size()){std::size_t n=k;next(word,n);if(cut&&measure(f,std::string_view(word).substr(0,n))>width)break;cut=k=n;}
   lines.push_back(word.substr(0,cut));i+=cut;
  }
  if(truncated){lines.push_back(fit(f,line+" "+std::string(p.substr(i)),width));break;}
  lines.push_back(std::move(line));
  if(end>=s.size())break;
  if(int(lines.size())>=maxLines){truncated=true;auto& last=lines.back();last=fit(f,last+" \xE2\x80\xA6",width);break;}
  para=end+1;
 }
 while(lines.size()>1&&lines.back().empty()&&!truncated)lines.pop_back();
 return lines;
}
int block(Canvas& c,Font f,Rect box,std::string_view s,std::uint32_t color,int lineHeight,int maxLines,Align a){
 int rows=std::max(1,std::min(maxLines,height(box)/std::max(1,lineHeight)));auto lines=wrap(f,s,float(width(box)),rows);
 float top=box.y0+(lineHeight-(ascent(f)+descent(f)))/2+ascent(f);int bottom=box.y0;
 for(std::size_t i=0;i<lines.size();++i){drawAligned(c,f,box,top+float(i)*lineHeight,lines[i],color,a);bottom=box.y0+int(i+1)*lineHeight;}
 return bottom;
}
void centeredLine(Canvas& c,Font f,Rect box,std::string_view s,std::uint32_t color,Align a){
 float cap=capHeight(f);float baseline=box.y0+(height(box)+cap)/2.f;drawAligned(c,f,box,baseline,fit(f,s,float(width(box))),color,a);
}
std::string quiet(std::string_view s){
 bool upper=false;for(char ch:s){if(ch>='a'&&ch<='z')return std::string(s);if(ch>='A'&&ch<='Z')upper=true;}
 if(!upper||s.size()<3)return std::string(s);
 std::string out;out.reserve(s.size()+8);bool start=true;
 for(std::size_t i=0;i<s.size();++i){char ch=s[i];
  if(ch==' '&&i+2<s.size()&&s[i+1]=='-'&&s[i+2]==' '){out+=" \xE2\x80\x94 ";i+=2;start=false;continue;}
  if(ch>='A'&&ch<='Z'){out+=start?ch:char(ch-'A'+'a');start=false;continue;}
  if(ch=='.'||ch=='!'||ch=='?')start=true;else if(ch!=' '&&ch!='"')start=false;
  out+=ch;}
 // Names and acronyms keep their usual case.
 auto word=[&](std::string_view from,std::string_view to){for(std::size_t at=0;(at=out.find(from,at))!=std::string::npos;at+=to.size()){
  bool left=at==0||!std::isalpha(static_cast<unsigned char>(out[at-1]));bool right=at+from.size()>=out.size()||!std::isalpha(static_cast<unsigned char>(out[at+from.size()]));
  if(left&&right)out.replace(at,from.size(),to);}};
 for(auto [a,b]:{std::pair<std::string_view,std::string_view>{"tom","Tom"},{"Tom","Tom"},{"wi-fi","Wi-Fi"},{"Wi-fi","Wi-Fi"},{"pdf","PDF"},{"Pdf","PDF"},{"epub","EPUB"},{"Epub","EPUB"},{"usb","USB"},{"Usb","USB"},{"ui","UI"}})word(a,b);
 return out;
}
}
