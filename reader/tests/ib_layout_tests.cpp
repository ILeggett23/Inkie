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
 // 2b. The reading view turns (docs/LANDSCAPE.md): in either landscape turn the reader chrome is the landscape zones,
 //     painters keep their targets inside them on an upright 2160x1620 canvas, and Frame maps both ways exactly.
 for(L::Turn turn:{L::Turn::Clockwise,L::Turn::Anticlockwise})for(Side side:{Side::Left,Side::Right}){L::setTurn(turn);reader::setToolbarSide(side);const auto z=L::zones(side,L::Orientation::Landscape);
  check(L::screenWidth()==2160&&L::screenHeight()==1620&&L::pageHeight()==1520,"landscape logical screen");
  check(same(reader::toolbarRect(),z.toolbar)&&same(reader::closeRect(),z.close)&&same(reader::footerRect(),z.footer)&&same(reader::penPanelRect(),z.palette)&&same(L::popoverBounds(z.toolbar),z.popover),"landscape chrome is the landscape zones");
  for(unsigned k=0;k<reader::ToolbarSlots;++k)check(inside(reader::toolbarSlot(k),z.toolbar),"landscape slot inside the toolbar");
  std::vector<std::uint32_t> px(std::size_t(L::logicalStride(turn))*1620,t::Paper);Canvas c({px,2160,1620,L::logicalStride(turn)});Targets tg;reader::Tools tools;tools.canUndo=tools.canRedo=true;reader::toolbar(c,tg,tools);for(auto x:list(tg))check(inside(x.rect,z.toolbar),"landscape toolbar targets inside the toolbar");
  Targets pt;reader::penPanel(c,pt,tools);for(auto x:list(pt))check(inside(x.rect,z.palette),"landscape palette targets inside the palette");
  Targets ft;reader::Chrome m;m.chapter="Chapter";m.page=3;m.total=40;reader::footer(c,ft,m);for(auto x:list(ft))check(inside(x.rect,z.footer),"landscape footer targets inside the footer");
  for(int y=140;y<1460;y+=170)for(int x=140;x<2040;x+=260){const Rect anchor{x,y,std::min(x+400,2120),y+56};Targets st;reader::SelectionBar bar;bar.anchor=anchor;bar.define=true;check(inside(reader::selectionToolbar(c,st,bar,L::popoverBounds(z.toolbar)),z.popover),"landscape strip inside the popover zone");}
  const L::Frame f{turn};check(f.width()==2160&&f.height()==1620,"frame size");
  // Corners and edges: the logical top edge is the panel's left (clockwise) or right (anticlockwise) edge.
  int px0,py0;f.physical(0,0,px0,py0);check(turn==L::Turn::Clockwise?(px0==0&&py0==2159):(px0==1619&&py0==0),"frame origin");
  check(same(f.physical(z.footer),turn==L::Turn::Clockwise?Rect{1520,0,1620,2160}:Rect{0,0,100,2160}),"landscape footer on the panel");
  check(same(f.physical(z.content),turn==L::Turn::Clockwise?Rect{0,0,1520,2160}:Rect{100,0,1620,2160}),"landscape page on the panel");
  for(Rect r:{z.toolbar,z.close,z.palette,Rect{17,23,1001,707}}){check(same(f.logical(f.physical(r)),r)&&width(f.physical(r))==height(r)&&height(f.physical(r))==width(r),"rect round trip");}
  for(int y=0;y<1620;y+=97)for(int x=0;x<2160;x+=113){int a,b,x2,y2;f.physical(x,y,a,b);check(a>=0&&a<1620&&b>=0&&b<2160&&f.physical(Rect{x,y,x+1,y+1}).x0==a&&f.physical(Rect{x,y,x+1,y+1}).y0==b,"point and pixel rect agree");f.logical(a,b,x2,y2);check(x2==x&&y2==y,"point round trip");}
  // Pixels: a logical rect copied out to the panel and back is unchanged, and lands where the rect maps.
  std::vector<std::uint32_t> panel(1632*2160,0),back(std::size_t(L::logicalStride(turn))*1620,0);for(std::size_t i=0;i<px.size();++i)px[i]=std::uint32_t(i*2654435761u);const Rect r{300,200,900,700};f.toPhysical(px.data(),L::logicalStride(turn),panel.data(),1632,r);f.toLogical(panel.data(),1632,back.data(),L::logicalStride(turn),r);
  bool ok=true;for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x){const auto i=std::size_t(y)*L::logicalStride(turn)+x;ok&=back[i]==px[i];int a,b;f.physical(x,y,a,b);ok&=panel[std::size_t(b)*1632+a]==px[i];}check(ok,"pixels round trip");}
 // 2c. Sheets turned (docs/LANDSCAPE.md, PR 3): Notes and Inkie keep composing in sheet pixels and SheetMap shows
 //     their lower 1620 rows upright, centred at the foot of the landscape screen. Every turned sheet paints and hits
 //     only in the rows shown; the writing areas (stored-stroke geometry) keep their 1480 width, and Inkie's their place.
 for(L::Turn turn:{L::Turn::Clockwise,L::Turn::Anticlockwise}){const L::SheetMap s{turn};const L::Frame f{turn};const Rect shown=s.visible();
  check(same(shown,{0,540,1620,2160})&&same(s.physical(shown),f.physical({270,0,1890,1620})),"the shown sheet rows stand at the foot, centred");
  check(same(s.physical({0,L::SheetTop,1620,2160}),f.physical({270,620,1890,1620}))&&same(L::zones(Side::Left,L::Orientation::Landscape).sheet,{270,620,1890,1620}),"the lower sheet at the foot, unchanged");
  check(same(s.physical({0,0,1620,600}),f.physical({270,0,1890,60}))&&s.physical({0,0,1620,540}).empty(),"rows above the shown window are not shown");
  check(same(s.sheet(s.physical(tom::WritingArea)),tom::WritingArea)&&same(s.sheet(f.physical({0,0,2160,1620})),{-270,540,1890,2160}),"sheet rects round trip");
  for(int y=540;y<2160;y+=89)for(int x=0;x<1620;x+=101){int a,b,x2,y2;s.physical(x,y,a,b);check(a>=0&&a<1620&&b>=0&&b<2160&&s.physical(Rect{x,y,x+1,y+1}).x0==a&&s.physical(Rect{x,y,x+1,y+1}).y0==b,"sheet point and pixel agree");s.sheet(a,b,x2,y2);check(x2==x&&y2==y,"sheet point round trip");}
  {int a,b,x,y;f.physical(100,800,a,b);s.sheet(a,b,x,y);check(x==-170&&y==1340,"the page beside a turned sheet is outside it");f.physical(2000,10,a,b);s.sheet(a,b,x,y);check(x==1730&&y==550,"right of the sheet");}
 }
 {const Rect shown=L::SheetMap{L::Turn::Clockwise}.visible();
  auto turnedSheet=[&](const char* what,auto paint){Frame fr;auto c=fr.canvas();paint(c,fr.tg);check(fr.tg.size()>0,what);for(auto x:list(fr.tg))check(inside(x.rect,shown),what);
   bool above=true;for(int y=0;y<shown.y0;++y)for(int x=0;x<t::ScreenWidth;++x)above&=fr.px[std::size_t(y)*t::ScreenWidth+x]==t::Paper;check(above,"nothing is painted above a turned sheet");return fr;};
  // Inkie: compact and tall composers, the answer sheet (with and without zoom), history and an earlier turn.
  auto inkie=[](int mode){tom::Model m;m.turned=true;m.hasSource=true;m.excerpt="It was the best of times, it was the worst of times";m.contextLine="On \xE2\x80\x9CIt was the best\xE2\x80\xA6\xE2\x80\x9D \xC2\xB7 p. 1";m.configured=true;m.writable=true;
   if(mode==1)m.expanded=true;if(mode==2||mode==3){m.inlineComposer=m.showAnswer=m.answered=true;m.composing=false;m.question="Why the opposites?";m.answer=Answer;m.revealInk=-1.f;m.zoomPercent=mode==3?150:0;}
   if(mode==4){m.history=true;m.threads={{"A question","2 turns"},{"Another","1 turn"}};m.turns={{"Why?",""},{"How?",""}};}if(mode==5){m.historyTurn=true;m.composing=false;m.showAnswer=true;m.question="Why?";m.answer=Answer;}return m;};
  for(int mode=0;mode<6;++mode){const auto m=inkie(mode);const auto fr=turnedSheet("turned Inkie paints and hits in the shown rows",[&](Canvas& c,Targets& tg){tom::paint(c,tg,m);});
   check(tom::sheetTop(m)>=L::TurnedSheetTop&&tom::sheetTop(m)==std::max(L::TurnedSheetTop,[&]{auto p=m;p.turned=false;return tom::sheetTop(p);}()),"turned Inkie starts at the portrait top, capped");
   if(mode==3){const Rect badge=tom::zoomBadgeRect(m);check(inside(badge,{0,tom::sheetTop(m),t::ScreenWidth,tom::sheetTop(m)+tom::HeaderHeight})&&!fr.tg.rect(reader::ZoomBadge).empty(),"turned zoom badge in the header");
    for(auto x:list(fr.tg))if(x.id!=reader::ZoomBadge)check(!overlaps(x.rect,fr.tg.rect(reader::ZoomBadge)),"turned zoom badge clear of the header's controls");}
   else check(fr.tg.rect(reader::ZoomBadge).empty()&&same(tom::zoomBadgeRect([&]{auto p=m;p.turned=false;return p;}()),reader::ZoomBadgeRect),"zoom badge only while zoomed; portrait keeps the top row");}
  for(Rect a:{tom::WritingArea,tom::ExpandedWritingArea,Rect{70,680,1550,1320}})check(inside(a,shown),"Inkie's writing and answer areas keep their place, shown");
  {Frame up,down;auto cu=up.canvas(),cd=down.canvas();auto m=inkie(1);m.turned=false;tom::paint(cu,up.tg,m);m.turned=true;tom::paint(cd,down.tg,m);bool paper=true;const Rect w=tom::ExpandedWritingArea;for(int y=w.y0;y<w.y1;++y)for(int x=w.x0;x<w.x1;++x)paper&=up.px[std::size_t(y)*t::ScreenWidth+x]==down.px[std::size_t(y)*t::ScreenWidth+x];check(paper,"the tall writing paper is the same paper turned");}
  // Notes: the note sheet (compact and expanded, with its pickers), the hub, a highlight and Layers.
  for(bool expanded:{false,true})for(int extra=0;extra<2;++extra){notes::Note n;n.turned=true;n.editable=true;n.hasExcerpt=true;n.excerpt="It was the best of times";n.context="Linked passage";n.favorites=true;n.expanded=expanded;n.picker=extra==1;n.inkActions=extra==1;
   turnedSheet("turned note paints and hits in the shown rows",[&](Canvas& c,Targets& tg){notes::note(c,tg,n);});
   const Rect a=notes::noteArea(expanded,true);check(inside(a,shown)&&width(a)==1480&&a.x0==notes::NoteArea.x0&&a.y1==notes::NoteArea.y1&&a.y0>=L::TurnedSheetTop+notes::NoteHeader,"turned note area: 1480 wide, below the header");
   check(inside(notes::notePicker(expanded),shown),"note picker shown");}
  check(same(notes::noteArea(false,true),notes::NoteArea)&&same(notes::noteArea(true,false),notes::NoteExpandedArea),"compact and portrait note areas unchanged");
  {notes::Hub h;h.turned=true;h.layers=true;h.pages=3;h.page=1;h.status="Saved";for(int i=0;i<6;++i)h.entries.push_back({"Note "+std::to_string(i),"An excerpt","A body","p. 1",t::Ink[4],i==0,true,true,false,false,{}});
   const auto fr=turnedSheet("turned hub paints and hits in the shown rows",[&](Canvas& c,Targets& tg){notes::hub(c,tg,h);});
   check(!fr.tg.rect(notes::EntryBase+notes::TurnedHubPageSize-1).empty()&&fr.tg.rect(notes::EntryBase+notes::TurnedHubPageSize).empty()&&notes::hubPageSize(true)==notes::TurnedHubPageSize&&notes::hubPageSize(false)==notes::HubPageSize,"turned hub: four notes a page");}
  {notes::HighlightDetail d;d.turned=true;d.excerpt="It was the best of times";d.source="Book \xC2\xB7 p. 1";turnedSheet("turned highlight paints and hits in the shown rows",[&](Canvas& c,Targets& tg){notes::highlight(c,tg,d);});}
  for(int menu=-1;menu<int(sheets::LayerPageSize);++menu){sheets::Layers l;l.rect=sheets::layersRect(true);for(int i=0;i<9;++i)l.layers.push_back({"Layer "+std::to_string(i),true,i==2,i==0,i==1,i});l.menuFor=menu;l.status="Saved";
   turnedSheet("turned Layers paints and hits in the shown rows",[&](Canvas& c,Targets& tg){sheets::layers(c,tg,l);});}
  check(inside(sheets::TurnedLayersRect,shown)&&width(sheets::TurnedLayersRect)==width(sheets::LayersRect)&&height(sheets::TurnedLayersRect)==height(sheets::LayersRect)&&same(sheets::layersRect(false),sheets::LayersRect),"turned Layers: the same side sheet, lower");
  check(inside(KeyboardRect,shown),"the keyboard is shown turned");}
 L::setTurn(L::Turn::Portrait);reader::setToolbarSide(Side::Left);check(L::screenWidth()==1620&&L::pageHeight()==2060&&same(reader::footerRect(),reader::FooterRect)&&same(L::Frame{}.physical(Rect{1,2,3,4}),Rect{1,2,3,4}),"portrait unchanged");
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
