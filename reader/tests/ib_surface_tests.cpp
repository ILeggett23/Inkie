// SPDX-License-Identifier: AGPL-3.0-only
// Whole-surface kit regressions over every ib-preview surface (the same models and painters the tablet uses), plus
// the text helpers those surfaces lean on. Pure C++, no Qt, no display:
//  - every registered target is at least the 96 px hit area, lies on the panel, and the registry never saturates
//    (Targets drops adds past its capacity, which would silently lose a control);
//  - high contrast and plain text change pixels only, never a target;
//  - text::fit / wrap / quiet keep their contracts on long and multi-byte strings;
//  - the pen-glyph cache keys on the disabled ink, so high contrast never reuses a normal-contrast raster.
//   ib-surface-tests <reader-dir>
#define main ib_preview_main
#include "../tools/ib_preview.cpp"
#undef main
#include "ib/icons.h"
#include <stdexcept>
namespace {
unsigned checks=0;
void check(bool ok,const std::string& what){++checks;if(!ok)throw std::runtime_error(what);}
struct Painted {std::vector<Target> targets;std::vector<std::uint32_t> px;};
Painted paint(const Surface& s){Image im;auto c=im.canvas();Targets tg;s.paint(c,tg);Painted p;for(unsigned i=0;i<tg.size();++i)p.targets.push_back(tg[i]);p.px=std::move(im.px);return p;}
bool sameTargets(const std::vector<Target>& a,const std::vector<Target>& b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i){const auto &x=a[i].rect,&y=b[i].rect;if(a[i].id!=b[i].id||x.x0!=y.x0||x.y0!=y.y0||x.x1!=y.x1||x.y1!=y.y1)return false;}return true;}
// Valid UTF-8 and no code point cut in half.
bool utf8(const std::string& s){for(std::size_t i=0;i<s.size();){const auto c=static_cast<unsigned char>(s[i]);const std::size_t n=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:0;if(!n||i+n>s.size())return false;for(std::size_t k=1;k<n;++k)if((static_cast<unsigned char>(s[i+k])>>6)!=2)return false;i+=n;}return true;}
}
int main(int argc,char** argv){try{
 if(argc!=2||!text::load(std::filesystem::path(argv[1])/"assets/fonts")){std::fprintf(stderr,"usage: ib-surface-tests <reader-dir>\n");return 2;}
 // 1. Targets: size, bounds and capacity on every surface, both hands; variants never move one.
 unsigned surfacesSeen=0,targetsSeen=0;
 for(const auto& s:surfaces())for(auto side:{reader::Side::Left,reader::Side::Right}){if(!s.sided&&side==reader::Side::Right)continue;
  reader::setToolbarSide(side);const std::string name=s.name+(side==reader::Side::Left?"":"-left-handed");
  t::setHighContrast(false);text::setPlainScript(false);const auto base=paint(s);++surfacesSeen;
  check(base.targets.size()<160,name+": target registry saturated (controls past 160 are dropped)");
  for(const auto& x:base.targets){++targetsSeen;const auto w=x.rect.x1-x.rect.x0,h=x.rect.y1-x.rect.y0;
   check(w>=96&&h>=96,name+": target "+std::to_string(x.id)+" smaller than the 96 px hit area ("+std::to_string(w)+"x"+std::to_string(h)+")");
   check(x.rect.x0>=0&&x.rect.y0>=0&&x.rect.x1<=t::ScreenWidth&&x.rect.y1<=t::ScreenHeight,name+": target "+std::to_string(x.id)+" off the panel");}
  t::setHighContrast(true);const auto contrast=paint(s);t::setHighContrast(false);
  check(sameTargets(base.targets,contrast.targets),name+": high contrast moved a target");
  text::setPlainScript(true);const auto plain=paint(s);text::setPlainScript(false);
  check(sameTargets(base.targets,plain.targets),name+": plain text moved a target");}
 reader::setToolbarSide(reader::Side::Left);
 check(surfacesSeen>=38&&targetsSeen>=560,"the preview registry still covers the product surfaces");
 // 2. Text helpers.
 const Font label{Face::Text,float(t::Label.size)},title{Face::Sans,float(t::Subhead.size)},body{Face::Serif,float(t::Body.size)};
 for(const Font& f:{label,title,body}){
  const std::string shortText="Recents";check(text::fit(f,shortText,900)==shortText,"fit keeps text that already fits");
  for(const std::string& s:{std::string(240,'x'),std::string("Café crème — naïve résumé, déjà vu, “quotes” and ünïcödé ")+std::string("éèêë",8)+"…"+std::string(60,'a')}){
   for(float width:{80.f,240.f,600.f}){const auto cut=text::fit(f,s,width);
    check(utf8(cut),"fit never splits a code point");check(text::measure(f,cut)<=width+0.5f,"fit result inside the width");
    check(cut.size()<s.size()&&cut.size()>=3&&cut.compare(cut.size()-3,3,"\xE2\x80\xA6")==0,"a cut ends with an ellipsis");}}
  std::string words;for(int i=0;i<60;++i)words+=(i%7==0?"extraordinarily ":"ink ");
  const auto lines=text::wrap(f,words,700);check(lines.size()>1,"long text wraps");std::string joined;
  for(const auto& l:lines){check(text::measure(f,l)<=700.5f,"every wrapped line fits");check(utf8(l),"wrap keeps code points whole");if(!joined.empty())joined+=' ';joined+=l;}
  while(!words.empty()&&words.back()==' ')words.pop_back();check(joined==words,"wrap loses no words");
  const auto capped=text::wrap(f,words,700,2);check(capped.size()==2&&capped.back().size()>=3&&capped.back().compare(capped.back().size()-3,3,"\xE2\x80\xA6")==0,"maxLines ellipsizes the last line");}
 check(text::quiet("PAGE STILL SAVING - TRY AGAIN")=="Page still saving \xE2\x80\x94 try again","quiet turns shouted status into a sentence");
 check(text::quiet("Already a sentence")=="Already a sentence"&&text::quiet("OK")=="OK","quiet leaves sentences and short codes");
 // 3. Pen-glyph cache: a disabled tip painted under each contrast matches a fresh paint under that contrast.
 auto tip=[](bool contrast){t::setHighContrast(contrast);Image im(120,120);auto c=im.canvas();penGlyph(c,Instrument::Fineliner,{20,20,100,100},0,false,true);t::setHighContrast(false);return im.px;};
 const auto normal=tip(false),contrast=tip(true),normalAgain=tip(false),contrastAgain=tip(true);
 check(normal!=contrast,"high contrast darkens a disabled tip (cache keyed on the disabled ink)");
 check(normal==normalAgain&&contrast==contrastAgain,"cached tips repaint identically");
 std::printf("IB_SURFACE_TESTS_PASS checks=%u surfaces=%u targets=%u\n",checks,surfacesSeen,targetsSeen);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"IB_SURFACE_TESTS_FAIL %s\n",e.what());return 1;}}
