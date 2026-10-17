// SPDX-License-Identifier: AGPL-3.0-only
// Deterministic host previews of the Inkbound presentation kit (src/ib). Every surface is painted by the same
// painters the device uses, into a 1620x2160 RGB32 buffer, and written as PNG. Painter-level only: the shell's
// compositing, refresh scheduling and the e-ink panel are not simulated (see docs/LAYOUT-ARCHITECTURE.md).
// Builds against older trees too: layout zones and the cursive role are used only when their headers exist.
//   ib-preview <reader-dir> <out-dir>
#include "ib/library_screen.h"
#include "ib/notes_screen.h"
#include "ib/reader_screen.h"
#include "ib/settings_screen.h"
#include "ib/sheets.h"
#include "ib/tom_screen.h"
#if __has_include("ib/layout.h")
#include "ib/layout.h"
#define IB_PREVIEW_LAYOUT 1
#endif
#include <zlib.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
namespace {
using namespace ib;
namespace t=theme;
constexpr int W=1620,H=2160;
struct Image {int w{W},h{H};std::vector<std::uint32_t> px;Image(int ww=W,int hh=H):w(ww),h(hh),px(std::size_t(ww)*hh,t::Paper){}Canvas canvas(){return Canvas({px,w,h,w});}};
void be32(std::string& s,std::uint32_t v){for(int i=3;i>=0;--i)s.push_back(char(v>>(i*8)&255));}
void chunk(std::ofstream& out,const char* type,const std::string& data){std::string c(type,4);c+=data;std::string len;be32(len,std::uint32_t(data.size()));std::string crc;be32(crc,std::uint32_t(crc32(0,reinterpret_cast<const Bytef*>(c.data()),uInt(c.size()))));out<<len<<c<<crc;}
// RGB8 PNG, one deflate stream, fixed compression level: identical input gives identical bytes.
bool png(const std::filesystem::path& path,const Image& im){
 std::string raw;raw.reserve(std::size_t(im.h)*(im.w*3+1));
 for(int y=0;y<im.h;++y){raw.push_back(0);for(int x=0;x<im.w;++x){auto p=im.px[std::size_t(y)*im.w+x];raw.push_back(char(p>>16&255));raw.push_back(char(p>>8&255));raw.push_back(char(p&255));}}
 uLongf n=compressBound(uLong(raw.size()));std::string z(n,'\0');if(compress2(reinterpret_cast<Bytef*>(z.data()),&n,reinterpret_cast<const Bytef*>(raw.data()),uLong(raw.size()),6)!=Z_OK)return false;z.resize(n);
 std::ofstream out(path,std::ios::binary);out<<"\x89PNG\r\n\x1a\n";std::string ihdr;be32(ihdr,std::uint32_t(im.w));be32(ihdr,std::uint32_t(im.h));ihdr+=std::string("\x08\x02\x00\x00\x00",5);
 chunk(out,"IHDR",ihdr);chunk(out,"IDAT",z);chunk(out,"IEND","");return bool(out);}
// A book page under the chrome: serif body text at the reader's measure.
void page(Canvas& c){
 c.fill({0,0,W,t::PageFitHeight},t::Paper);
 const char* para="It was the best of times, it was the worst of times, it was the age of wisdom, it was the age of foolishness, it was the epoch of belief, it was the epoch of incredulity, it was the season of Light, it was the season of Darkness, it was the spring of hope, it was the winter of despair.";
 int y=180;for(int i=0;i<6&&y<1900;++i)y=text::block(c,{Face::Serif,36},{170,y,1450,2000},para,t::InkPrimary,58,6)+40;
}
std::vector<library::Document> books(){std::vector<library::Document> d;const char* titles[]{"A Tale of Two Cities","Middlemarch","The Origin of Species","Field Notes","Quarterly report","Walden"};const char* fmt[]{"epub","epub","epub","pdf","pdf","epub"};
 for(int i=0;i<6;++i)d.push_back({titles[i],std::string("Opened ")+std::to_string(i+1)+" d ago \xC2\xB7 "+std::to_string(12+i*7)+"%",fmt[i],{},i==1});return d;}
library::Model libraryModel(){library::Model m;m.battery=73;m.wifi=true;m.clock="9:41";m.folders={{"Reading",12},{"Research",4},{"Notebooks",7}};m.documents=books();m.pages=3;
 m.createItems={{900,Icon::Note,"Notebook","Lined, grid, dot or blank pages"}};m.drawerExtras={{901,Icon::Back,"Leave Inkbound",{}}};return m;}
tom::Model tomModel(){tom::Model m;m.contextLine="A Tale of Two Cities \xC2\xB7 page 12";m.excerpt="It was the best of times, it was the worst of times";m.hasSource=true;m.writable=true;m.empty=true;m.canSend=false;
 m.disclosure="Tom sees this passage and your question.";return m;}
const char* Answer="Dickens opens with paired opposites to show a society pulled two ways at once. The rhythm makes the reader feel the instability of 1775 before any character appears.";
struct Surface {std::string name;std::function<void(Canvas&,Targets&)> paint;bool sided{};};
std::vector<Surface> surfaces(){
 std::vector<Surface> s;
 s.push_back({"library-grid",[](Canvas& c,Targets& tg){library::paint(c,tg,libraryModel());}});
 s.push_back({"library-list",[](Canvas& c,Targets& tg){auto m=libraryModel();m.grid=false;library::paint(c,tg,m);}});
 s.push_back({"library-drawer",[](Canvas& c,Targets& tg){auto m=libraryModel();m.drawer=true;library::paint(c,tg,m);}});
 s.push_back({"library-create",[](Canvas& c,Targets& tg){auto m=libraryModel();m.create=true;library::paint(c,tg,m);}});
 s.push_back({"library-empty",[](Canvas& c,Targets& tg){auto m=libraryModel();m.documents.clear();m.folders.clear();m.pages=1;library::paint(c,tg,m);}});
 s.push_back({"library-notes-hub",[](Canvas& c,Targets& tg){auto m=libraryModel();m.section=library::Section::Notes;m.heading="Notes";m.sortLabel.clear();m.documents.clear();m.folders.clear();library::base(c,tg,m);
  notes::Hub h;h.embedded=true;h.canCreate=false;
  auto entry=[&](std::string title,std::string excerpt,std::string body,std::string source,std::uint32_t color,bool favorite,bool tom){notes::Entry e;e.title=title;e.excerpt=excerpt;e.body=body;e.source=source;e.color=color;e.favorite=favorite;e.linked=!excerpt.empty();e.tom=tom;h.entries.push_back(e);};
  entry("On paired opposites","It was the best of times","","A Tale of Two Cities \xC2\xB7 p. 12",t::Ink[4],true,false);entry("Reading list","","Finish Middlemarch, then Walden.","Notebook",t::Ink[6],false,false);entry("Ask about 1775","","","Tom \xC2\xB7 yesterday",t::Ink[0],false,true);
  notes::hub(c,tg,h);library::overlays(c,tg,m);}});
 s.push_back({"library-notes-empty",[](Canvas& c,Targets& tg){auto m=libraryModel();m.section=library::Section::Notes;m.heading="Notes";m.sortLabel.clear();m.documents.clear();m.folders.clear();library::base(c,tg,m);notes::Hub h;h.embedded=true;h.canCreate=false;notes::hub(c,tg,h);library::overlays(c,tg,m);}});
 for(auto p:{settings::Page::Reading,settings::Page::Accessibility,settings::Page::Tom})
  s.push_back({std::string("settings-")+settings::pageName(p),[p](Canvas& c,Targets& tg){auto m=libraryModel();m.section=library::Section::Settings;m.documents.clear();m.folders.clear();library::base(c,tg,m);settings::Model sm;sm.page=p;sm.tomOnline=1;settings::paint(c,tg,sm);library::overlays(c,tg,m);}});
 s.push_back({"reader-clean",[](Canvas& c,Targets& tg){page(c);reader::Chrome ch;ch.chapter="Book the First \xE2\x80\x94 Recalled to Life";ch.page=11;ch.total=412;reader::footer(c,tg,ch);reader::Tools tools;reader::toolHandle(c,tg,tools);}});
 s.push_back({"reader-chrome",[](Canvas& c,Targets& tg){page(c);reader::Chrome ch;ch.chapter="Book the First \xE2\x80\x94 Recalled to Life";ch.page=11;ch.total=412;reader::footer(c,tg,ch);reader::Tools tools;tools.canUndo=true;reader::toolbar(c,tg,tools);reader::closeButton(c,tg);},true});
 s.push_back({"reader-palette",[](Canvas& c,Targets& tg){page(c);reader::Tools tools;tools.activeShortcut=0;tools.panel=true;reader::toolbar(c,tg,tools);reader::closeButton(c,tg);reader::penPanel(c,tg,tools);},true});
 s.push_back({"reader-highlighter-palette",[](Canvas& c,Targets& tg){page(c);reader::Tools tools;tools.activeShortcut=1;tools.highlighter=true;tools.panel=true;reader::toolbar(c,tg,tools);reader::closeButton(c,tg);reader::penPanel(c,tg,tools);},true});
 s.push_back({"reader-document-menu",[](Canvas& c,Targets& tg){page(c);reader::Tools tools;tools.menu=true;reader::toolbar(c,tg,tools);reader::closeButton(c,tg);
  std::vector<sheets::MenuItem> items{{1,Icon::BookOpen,"Text and layout"},{2,Icon::Pages,"Page overview"},{3,Icon::Bookmark,"Bookmark this page"},{4,Icon::Notes,"Document notes"},{5,Icon::Tom,"Ask Tom about this page"},{6,Icon::Info,"About this book"}};
  sheets::documentMenu(c,tg,reader::toolbarSlot(reader::SlotDocument),"A Tale of Two Cities",items,3);},true});
 s.push_back({"reader-selection",[](Canvas& c,Targets& tg){page(c);Rect lines[]{{170,700,1450,758},{170,758,900,816}};reader::selectionWash(c,lines);reader::SelectionBar b;b.anchor={170,700,1450,816};b.define=false;auto bar=reader::selectionToolbar(c,tg,b);reader::highlightColors(c,tg,bar,b.anchor,4,false);}});
 s.push_back({"reader-definition",[](Canvas& c,Targets& tg){page(c);reader::Definition d;d.word="incredulity";d.pronunciation="noun";d.senses={{"noun","doubt about the truth of something; the state of being unwilling or unable to believe"}};d.attribution="WordNet 3.0 \xC2\xB7 Princeton University";reader::definitionCard(c,tg,{700,940,900,990},d);}});
 s.push_back({"reader-quick-browse",[](Canvas& c,Targets& tg){page(c);sheets::QuickBrowse q;q.title="A Tale of Two Cities";q.position="3%";q.page=11;q.total=412;q.start=4;sheets::quickBrowse(c,tg,q);}});
 s.push_back({"reader-recents",[](Canvas& c,Targets& tg){page(c);std::vector<sheets::RecentBook> r{{"A Tale of Two Cities","EPUB","epub",true},{"Middlemarch","EPUB","epub"},{"Field Notes","PDF","pdf"}};sheets::recents(c,tg,r);}});
 s.push_back({"reader-zoom-badge",[](Canvas& c,Targets& tg){page(c);reader::zoomBadge(c,tg,150);}});
 s.push_back({"note-sheet",[](Canvas& c,Targets& tg){page(c);notes::Note n;n.context="A Tale of Two Cities \xC2\xB7 page 12";n.excerpt="It was the best of times";n.hasExcerpt=true;n.linked=true;n.editable=true;n.favorites=true;n.empty=true;notes::note(c,tg,n);}});
 s.push_back({"tom-write",[](Canvas& c,Targets& tg){page(c);tom::paint(c,tg,tomModel());}});
 s.push_back({"tom-handwriting-reply",[](Canvas& c,Targets& tg){page(c);auto m=tomModel();m.showAnswer=true;m.empty=false;m.question="Why open with opposites?";m.answer=Answer;m.answered=true;m.canSaveNote=true;tom::paint(c,tg,m);}});
 s.push_back({"tom-typed-reply",[](Canvas& c,Targets& tg){page(c);auto m=tomModel();m.showAnswer=true;m.empty=false;m.typed=true;m.question="Why open with opposites?";m.answer=Answer;m.answered=true;m.canSaveNote=true;tom::paint(c,tg,m);}});
 s.push_back({"tom-history",[](Canvas& c,Targets& tg){page(c);auto m=tomModel();m.history=true;m.threads={{"Paired opposites","A Tale of Two Cities \xC2\xB7 2 questions"},{"Dorothea's choice","Middlemarch \xC2\xB7 1 question"}};m.turns={{"Why open with opposites?",Answer}};tom::paint(c,tg,m);}});
 s.push_back({"sheet-layers",[](Canvas& c,Targets& tg){page(c);sheets::Layers l;l.layers={{"Annotations",true,false,true,true,14},{"Highlights",true,false,false,false,6},{"Sketch",false,true,false,false,2}};sheets::layers(c,tg,l);}});
 s.push_back({"sheet-document-settings",[](Canvas& c,Targets& tg){page(c);sheets::Settings st;st.pdf=false;st.tab=sheets::Tab::Reading;st.pages=412;sheets::settings(c,tg,st);}});
 s.push_back({"sheet-new-notebook",[](Canvas& c,Targets& tg){auto m=libraryModel();library::paint(c,tg,m);sheets::NewNotebook n;n.title="Reading notes";n.folder="My files";sheets::newNotebook(c,tg,n);}});
 s.push_back({"keyboard",[](Canvas& c,Targets& tg){page(c);KeyboardModel k;k.title="Rename";k.value="Reading notes";keyboard(c,tg,k);}});
 return s;
}
#ifdef IB_PREVIEW_LAYOUT
// Zone map: each zone washed in its own colour, outlined and labelled.
void zoneMap(Image& im,const layout::Zones& z,std::string_view title){
 auto c=im.canvas();c.fill({0,0,im.w,im.h},t::Paper);
 auto box=[&](Rect r,std::uint32_t col,std::string_view label){if(r.empty())return;
  c.multiply(r,t::wash(col,70));c.hairline(r,col,3);text::draw(c,{Face::Text,24},float(r.x0+10),float(r.y0+30),label,t::InkPrimary);};
 box(z.content,0xff567db8u,"content");box(z.sheet,0xff55905au,"sheet");box(z.palette,0xffd78b39u,"palette");box(z.strip,0xff9467acu,"strip band");
 box(z.toolbar,0xff1c2028u,"toolbar");box(z.close,0xff1c2028u,"close");box(z.footer,0xff8c8c8cu,"footer");
 box(z.reservedTop,0xffbd5250u,"edge: recents / drawer");box(z.reservedStatus,0xffbd5250u,"quick settings");box(z.reservedBottom,0xffbd5250u,"edge: quick browse");
 text::draw(c,{Face::TextSemibold,36},float(im.w/2-300),float(im.h/2),title,t::InkPrimary);}
#endif
}
int main(int argc,char** argv){
 if(argc!=3){std::fprintf(stderr,"usage: ib-preview <reader-dir> <out-dir>\n");return 2;}
 const std::filesystem::path reader=argv[1],out=argv[2];if(!text::load(reader/"assets/fonts")){std::fprintf(stderr,"fonts missing\n");return 2;}
 std::filesystem::create_directories(out);unsigned written=0;
 auto save=[&](const Image& im,const std::string& name){if(!png(out/(name+".png"),im)){std::fprintf(stderr,"write failed %s\n",name.c_str());std::exit(1);}++written;};
 struct Variant {std::string suffix;bool contrast,plain;};
 std::vector<Variant> variants{{"",false,false},{"-high-contrast",true,false}};
#if __has_include("ib/type.h")
 variants.push_back({"-plain",false,true});
#endif
 for(const auto& v:variants){t::setHighContrast(v.contrast);
#if __has_include("ib/type.h")
  text::setPlainScript(v.plain);
#endif
  for(const auto& s:surfaces())for(auto side:{reader::Side::Left,reader::Side::Right}){if(!s.sided&&side==reader::Side::Right)continue;reader::setToolbarSide(side);
   Image im;auto c=im.canvas();Targets tg;s.paint(c,tg);save(im,s.name+(s.sided?(side==reader::Side::Left?"-right-handed":"-left-handed"):"")+v.suffix);}}
 t::setHighContrast(false);reader::setToolbarSide(reader::Side::Left);
#if __has_include("ib/type.h")
 text::setPlainScript(false);
#endif
#ifdef IB_PREVIEW_LAYOUT
 for(auto o:{layout::Orientation::Portrait,layout::Orientation::Landscape})for(auto side:{reader::Side::Left,reader::Side::Right}){
  const auto z=layout::zones(side,o);Image im(width(z.screen),height(z.screen));const bool land=o==layout::Orientation::Landscape;
  zoneMap(im,z,std::string(land?"Landscape (specification; not reachable at runtime)":"Portrait")+(side==reader::Side::Left?" \xC2\xB7 right-handed":" \xC2\xB7 left-handed"));
  save(im,std::string("zones-")+(land?"landscape":"portrait")+(side==reader::Side::Left?"-right-handed":"-left-handed"));}
#endif
 std::printf("IB_PREVIEW_PASS images=%u\n",written);return 0;
}
