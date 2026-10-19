// SPDX-License-Identifier: AGPL-3.0-only
// Layout architecture and cursive-role regressions (docs/LAYOUT-ARCHITECTURE.md). Pure presentation-kit checks: zones
// equal the shipping rects for both hands, painters register targets inside their zone, transient popovers keep
// clear of the toolbar column and top chrome row, and switching cursive off changes glyphs only, never a target rect.
//   ib-layout-tests <reader-dir>
#include "ib/layout.h"
#include "ib/library_screen.h"
#include "ib/notes_screen.h"
#include "ib/reader_screen.h"
#include "ib/settings_screen.h"
#include "ib/sheets.h"
#include "ib/tom_screen.h"
#include "ib/type.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using namespace ib;
namespace t=theme;
unsigned checks=0;
void check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
bool same(Rect a,Rect b){return a.x0==b.x0&&a.y0==b.y0&&a.x1==b.x1&&a.y1==b.y1;}
bool inside(Rect r,Rect zone){return r.x0>=zone.x0&&r.y0>=zone.y0&&r.x1<=zone.x1&&r.y1<=zone.y1;}
struct Frame {std::vector<std::uint32_t> px=std::vector<std::uint32_t>(std::size_t(t::ScreenWidth)*t::ScreenHeight,t::Paper);Targets tg;Canvas canvas(){return Canvas({px,t::ScreenWidth,t::ScreenHeight,t::ScreenWidth});}};
std::vector<Target> list(const Targets& tg){std::vector<Target> out;for(unsigned i=0;i<tg.size();++i)out.push_back(tg[i]);return out;}
bool sameTargets(const Targets& a,const Targets& b){auto x=list(a),y=list(b);if(x.size()!=y.size())return false;for(std::size_t i=0;i<x.size();++i)if(x[i].id!=y[i].id||!same(x[i].rect,y[i].rect))return false;return true;}
library::Model libraryModel(){library::Model m;m.folders={{"Reading",3}};for(int i=0;i<4;++i)m.documents.push_back({"Book "+std::to_string(i),"Opened today","epub",{},false});return m;}
const char* Answer="Dickens opens with paired opposites to show a society pulled two ways at once, and the rhythm carries it.";
}
int main(int argc,char** argv){try{
 if(argc!=2||!text::load(std::filesystem::path(argv[1])/"assets/fonts"))return 2;
 using reader::Side;namespace L=layout;
 // 1. Zones are the shipping geometry for both hands; painting and hit testing read the same rects.
 for(Side side:{Side::Left,Side::Right}){reader::setToolbarSide(side);const auto z=L::zones(side);
  check(same(z.toolbar,reader::toolbarRect())&&same(z.close,reader::closeRect())&&same(z.palette,reader::penPanelRect())&&same(z.footer,reader::FooterRect),"zones equal reader chrome rects");
  check(same(z.content,{0,0,t::ScreenWidth,t::PageFitHeight})&&same(z.reservedBottom,z.footer)&&z.reservedTop.y1==L::EdgeBand&&z.reservedStatus.x0==L::StatusCornerX,"content and gesture bands");
  check(z.sheet.y0==tom::SheetTop&&z.sheet.y0==notes::NoteSheetTop&&z.sheetExpanded.y0==notes::NoteExpandedTop&&tom::ExpandedSheetTop>=z.sheetExpanded.y0,"one lower-sheet geometry");
  check(height(z.sheet)*1000/t::ScreenHeight<=463,"lower sheet at most 46.3% of the panel");
  check(!overlaps(z.toolbar,z.strip)&&!overlaps(z.close,z.strip)&&z.strip.y0==L::TopRow,"strip band clear of toolbar and top row");
  // Writing-hand rule: the toolbar sits on the side away from the writing hand, the close on the far top corner.
  check(side==Side::Left?(z.toolbar.x0==0&&z.close.x1==t::ScreenWidth):(z.toolbar.x1==t::ScreenWidth&&z.close.x0==0),"toolbar opposite the writing hand");
  // Every toolbar target is inside the toolbar column (slot geometry shared with input routing).
  {Frame f;auto c=f.canvas();reader::Tools tools;tools.canUndo=tools.canRedo=true;reader::toolbar(c,f.tg,tools);for(auto x:list(f.tg))check(inside(x.rect,z.toolbar),"toolbar target inside its column");
   for(unsigned s=0;s<reader::ToolbarSlots;++s)check(inside(reader::toolbarSlot(s),z.toolbar)&&height(reader::toolbarSlot(s))>=t::MinTarget,"slot inside column at full target size");}
  // Palette cells are inside the attached palette.
  {Frame f;auto c=f.canvas();reader::Tools tools;reader::penPanel(c,f.tg,tools);for(auto x:list(f.tg))check(inside(x.rect,z.palette),"palette target inside palette zone");}
  // Transient popovers: a grid of selections across the page, chrome shown. Never over the toolbar or top row.
  const Rect bounds=L::popoverBounds(z.toolbar);check(same(bounds,z.popover),"popover bounds are the popover zone");
  for(int y=140;y<2000;y+=170)for(int x=140;x<1500;x+=260){const Rect anchor{x,y,std::min(x+400,1580),y+56};
   Frame f;auto c=f.canvas();reader::SelectionBar bar;bar.anchor=anchor;bar.define=true;const Rect strip=reader::selectionToolbar(c,f.tg,bar,bounds);
   check(inside(strip,bounds),"selection strip inside popover bounds");
   reader::Definition d;d.word="word";d.senses={{"noun","a single distinct meaningful element of speech or writing"}};Frame g;auto gc=g.canvas();const Rect card=reader::definitionCard(gc,g.tg,anchor,d,bounds);
   check(!overlaps(card,z.toolbar)&&card.y0>=L::TopRow,"definition card clear of toolbar and top row");}
 }
 reader::setToolbarSide(Side::Left);
 // 2. Landscape is a specification: it must still be a coherent system.
 for(Side side:{Side::Left,Side::Right}){const auto z=L::zones(side,L::Orientation::Landscape);
  check(width(z.screen)==t::ScreenHeight&&height(z.screen)==t::ScreenWidth,"landscape screen");
  for(Rect r:{z.content,z.footer,z.toolbar,z.close,z.palette,z.strip,z.sheet,z.reservedTop,z.reservedStatus,z.reservedBottom})check(inside(r,z.screen),"landscape zone on screen");
  check(!overlaps(z.toolbar,z.sheet)&&!overlaps(z.toolbar,z.strip)&&inside(z.palette,z.content),"landscape zones disjoint where they must be");}
 // 3. Every role size is at least the smallest permitted text.
 for(auto r:{t::Code,t::Display,t::Title,t::Heading,t::Subhead,t::Body,t::Label,t::Control,t::Meta,t::Caption})check(r.size>=t::Caption.size&&r.line>r.size,"role sizes");
 // 4. Cursive roles: the face swap is resolved in the shared text path, sizes stay legible, widths stay close.
 for(Font f:{type::Voice,type::ScriptHeading,type::ScriptTitle,type::ScriptEmpty}){
  check(f.face==Face::Script&&f.size>=type::ScriptMinimumRole,"cursive role size");
  text::setPlainScript(false);const Font cursive=text::resolved(f);check(cursive.face==Face::Script&&cursive.size==f.size*ScriptScale,"cursive draws Caveat at ScriptScale");
  const float wc=text::measure(f,"The quick brown fox jumps over the lazy dog, 1775.");
  text::setPlainScript(true);const Font plain=text::resolved(f);check(plain.face==f.plain&&plain.size==f.size,"plain draws the named face at the role size");
  const float wp=text::measure(f,"The quick brown fox jumps over the lazy dog, 1775.");check(wp==text::measure({f.plain,f.size},"The quick brown fox jumps over the lazy dog, 1775."),"plain measures as the plain face");
  check(wc>wp*0.88f&&wc<wp*1.12f,"cursive and plain set within 12% of each other");
  check(text::capHeight(f)>0&&text::ascent(f)>0,"metrics");}
 text::setPlainScript(false);
 // Tom's typed reply follows the handwriting-size preference and lands on Tom's pen sizes (Small within 0.5 px).
 {const float pen[]{42.5f,50,60};for(unsigned i=0;i<3;++i){check(text::resolved(type::voice(i)).size==pen[i]&&type::voiceLine(i)>=int(type::voice(i).size*1.5f),"typed cursive follows hand size");}check(type::voice(0).size==type::Voice.size,"Small is the Voice role");}
 // 5. Switching cursive off changes glyphs inside unchanged rects: every surface registers identical targets.
 // Words are written and data printed (text.h ScriptFrom): a surface with only small control labels (the selection
 // strip) prints the same in both modes; every other surface shows its cursive.
 auto both=[&](const char* name,auto paint,bool printed=false){Frame a,b;auto ca=a.canvas(),cb=b.canvas();text::setPlainScript(false);paint(ca,a.tg);text::setPlainScript(true);paint(cb,b.tg);text::setPlainScript(false);
  check(sameTargets(a.tg,b.tg),name);check(printed?a.px==b.px:a.px!=b.px,printed?"printed surface is identical in both modes":"cursive is visible on the surface");};
 both("library heading targets unchanged",[](Canvas& c,Targets& tg){library::paint(c,tg,libraryModel());});
 both("empty library targets unchanged",[](Canvas& c,Targets& tg){auto m=libraryModel();m.documents.clear();library::paint(c,tg,m);});
 both("settings targets unchanged",[](Canvas& c,Targets& tg){settings::Model m;m.page=settings::Page::Accessibility;settings::paint(c,tg,m);});
 both("notes hub targets unchanged",[](Canvas& c,Targets& tg){notes::Hub h;notes::Entry e;e.title="Paired opposites";e.excerpt="It was the best of times";h.entries={e,e};notes::hub(c,tg,h);});
 both("tom typed reply targets unchanged",[](Canvas& c,Targets& tg){tom::Model m;m.showAnswer=true;m.typed=true;m.answer=Answer;m.question="Why?";tom::paint(c,tg,m);});
 // Tom's pen reply never changes with the switch: his strokes are not text (the writing area is pixel-identical).
 {Frame a,b;auto ca=a.canvas(),cb=b.canvas();tom::Model m;m.showAnswer=true;m.answer=Answer;text::setPlainScript(false);tom::paint(ca,a.tg,m);text::setPlainScript(true);tom::paint(cb,b.tg,m);text::setPlainScript(false);
  const Rect area{tom::WritingArea.x0,tom::WritingArea.y0,tom::WritingArea.x1-t::MinTarget,tom::WritingArea.y1};bool same=true;
  for(int y=area.y0;y<area.y1&&same;++y)for(int x=area.x0;x<area.x1;++x)if(a.px[std::size_t(y)*t::ScreenWidth+x]!=b.px[std::size_t(y)*t::ScreenWidth+x]){same=false;break;}
  check(same,"Tom's pen strokes unaffected by plain text");}
 // The toolbar carries no text: pixel-identical in both modes. The palette's labels change face, never its targets.
 {Frame a,b;auto ca=a.canvas(),cb=b.canvas();reader::Tools tools;text::setPlainScript(false);reader::toolbar(ca,a.tg,tools);text::setPlainScript(true);reader::toolbar(cb,b.tg,tools);text::setPlainScript(false);check(a.px==b.px,"toolbar identical in both modes");}
 both("pen palette targets unchanged",[](Canvas& c,Targets& tg){reader::Tools tools;reader::penPanel(c,tg,tools);});
 both("selection strip targets unchanged",[](Canvas& c,Targets& tg){reader::SelectionBar bar;bar.anchor={300,900,1200,960};bar.define=true;reader::selectionToolbar(c,tg,bar);},true);
 both("document settings targets unchanged",[](Canvas& c,Targets& tg){sheets::Settings st;st.pdf=false;st.tab=sheets::Tab::Reading;sheets::settings(c,tg,st);});
 // 6. The settings row exists, is a full-size control, and sits below every existing Accessibility row (none moved:
 //    High contrast and Reduce motion keep the positions they had before the row was added).
 {Frame f;auto c=f.canvas();settings::Model m;m.page=settings::Page::Accessibility;settings::paint(c,f.tg,m);const Rect r=f.tg.rect(settings::PlainText);check(!r.empty()&&width(r)>=t::MinTarget&&height(r)>=t::MinTarget,"plain text toggle is a full target");
  const Rect hc=f.tg.rect(settings::HighContrast),rm=f.tg.rect(settings::ReduceMotion);check(r.y0>=rm.y1&&hc.y0==374&&rm.y0==666,"existing accessibility rows unmoved (base: 374 and 666)");}
 // 7. The highlight detail sheet shares the lower-sheet geometry: its close sits where Note's does.
 {Frame a,b;auto ca=a.canvas(),cb=b.canvas();notes::HighlightDetail h;h.excerpt="It was the best of times";notes::highlight(ca,a.tg,h);notes::Note n;n.editable=true;notes::note(cb,b.tg,n);check(same(a.tg.rect(notes::HlClose),b.tg.rect(notes::NoteDone)),"one close position for lower sheets");}
 std::printf("IB_LAYOUT_PASS checks=%u\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"IB_LAYOUT_FAIL %s\n",e.what());return 1;}}
