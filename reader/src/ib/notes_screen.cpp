// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/notes_screen.h"
#include "ib/reader_screen.h"
#include "ib/type.h"
#include <algorithm>
#include <array>
namespace ib::notes {
namespace t=theme;
namespace {
const Font Caption{Face::Text,float(t::Caption.size)};
constexpr std::array<std::string_view,5> Filters{"All","Linked","Pages","Favorites","Tom"};
void status(Canvas& c,std::string_view s,bool warn=false){if(!s.empty())text::draw(c,Caption,70.f,float(t::ScreenHeight-26),text::fit(Caption,s,1480),warn?t::InkPrimary:t::InkTertiary);}
}
void hub(Canvas& c,Targets& tg,const Hub& m){const int o=m.idOffset;
 int x0=m.embedded?library::ContentLeft:70,x1=m.embedded?library::ContentRight:t::ScreenWidth-70;int y;
 if(!m.embedded){contextSheet(c,120,t::SectionColor[1]);y=120; /* the native square sheet, as Note and Tom */
  text::draw(c,type::ScriptHeading,float(x0),float(y+72),m.heading,t::InkPrimary);
  if(!m.subtitle.empty())text::draw(c,{Face::TextItalic,float(t::Meta.size)},float(x0+text::measure(type::ScriptHeading,m.heading)+t::S3),float(y+70),text::fit({Face::TextItalic,float(t::Meta.size)},m.subtitle,700),t::InkSecondary);
  iconButton(c,tg,HubClose+o,{x1-t::MinTarget+t::S2,y+8,x1+t::S2,y+t::TopBar},Icon::Close);
  if(m.canCreate)iconButton(c,tg,HubCreate+o,{x1-2*t::MinTarget+t::S2,y+8,x1-t::MinTarget+t::S2,y+t::TopBar},Icon::Plus);
  if(m.layers)iconButton(c,tg,HubLayers+o,{x1-3*t::MinTarget+t::S2,y+8,x1-2*t::MinTarget+t::S2,y+t::TopBar},Icon::Layers);
  y+=t::TopBar+t::S3;}
 else y=library::NoticeRect.y1; /* embedded in My Files: the library draws the "Notes" heading */
 std::size_t filters=m.favorites?Filters.size():Filters.size()-1;std::vector<std::string_view> labels(Filters.begin(),Filters.end());if(!m.favorites)labels.erase(labels.begin()+3);
 chips(c,tg,FilterBase+o,{x0,y,x1,y+64},std::span(labels.data(),filters),m.favorites||unsigned(m.filter)<3?unsigned(m.filter):unsigned(m.filter)-1);y+=64+t::S4;
 if(m.entries.empty())text::draw(c,type::ScriptEmpty,float(x0),float(y+90),m.filter==Filter::All?"Notes you write while reading appear here.":"No notes in this view.",t::InkSecondary);
 const int rowH=250;
 for(std::size_t i=0;i<m.entries.size()&&i<std::size_t(HubPageSize);++i){const auto& e=m.entries[i];Rect r{x0,y+int(i)*rowH,x1,y+int(i+1)*rowH};
  Rect thumb{r.x0,r.y0+t::S4,r.x0+128,r.y1-t::S4};c.fillRound(thumb,t::RadiusMedium,t::Surface);c.strokeRound(thumb,t::RadiusMedium,float(t::Outline),t::InkPrimary);{const Rect tab{thumb.x0,thumb.y0+t::S3,thumb.x0+14,thumb.y1-t::S3};c.fill(tab,e.color);c.hairline(tab,t::InkPrimary,t::Outline);}
  if(!e.preview.empty())c.image(inset(thumb,10),e.preview.pixels,e.preview.width,e.preview.height,e.preview.stride);
  else icon(c,e.tom?Icon::Tom:e.handwriting?Icon::Handwrite:e.linked?Icon::Highlight:Icon::Notes,centered(thumb,48,48),t::InkSecondary);
  int tx=thumb.x1+t::S4,tr=r.x1-t::MinTarget;int ty=r.y0+t::S4+36;
  text::draw(c,type::ScriptTitle,float(tx),float(ty),text::fit(type::ScriptTitle,e.title,float(tr-tx)),t::InkPrimary);ty+=t::S1;
  std::string quoteText=!e.excerpt.empty()?"\xE2\x80\x9C"+e.excerpt+"\xE2\x80\x9D":e.body;
  auto lines=text::wrap({e.excerpt.empty()?Face::Serif:Face::SerifItalic,theme::Label.size},quoteText,float(tr-tx),2);for(std::size_t k=0;k<lines.size();++k)text::draw(c,{e.excerpt.empty()?Face::Serif:Face::SerifItalic,theme::Label.size},float(tx),float(ty+40+int(k)*42),lines[k],t::InkSecondary);
  text::draw(c,Caption,float(tx),float(r.y1-t::S4-8),text::fit(Caption,e.unresolved?"Source changed \xE2\x80\x94 note kept":e.source,float(tr-tx)),e.unresolved?t::InkPrimary:t::InkTertiary);
  if(m.favorites)iconButton(c,tg,StarBase+o+int(i),{r.x1-t::MinTarget,r.y0+t::S3,r.x1,r.y0+t::S3+t::MinTarget},e.favorite?Icon::StarFilled:Icon::Star,{e.favorite,false,t::PopYellow},36);
  divider(c,r.x0,r.x1,r.y1);tg.add(EntryBase+o+int(i),{r.x0,r.y0,r.x1-t::MinTarget,r.y1});}
 if(m.pages>1){int py=m.embedded?library::PillRect.y0:t::ScreenHeight-150;int cx=m.embedded?(1160+library::ContentRight)/2:(x0+x1)/2; /* embedded: right of the create pill */text::centeredLine(c,{Face::Sans,float(t::Meta.size)},{cx-100,py,cx+100,py+t::MinTarget},std::to_string(m.page+1)+" of "+std::to_string(m.pages),t::InkSecondary);
  iconButton(c,tg,HubPrev+o,{cx-220,py,cx-120,py+t::MinTarget},Icon::ChevronLeft,{false,m.page<=0});iconButton(c,tg,HubNext+o,{cx+120,py,cx+220,py+t::MinTarget},Icon::ChevronRight,{false,m.page+1>=m.pages});}
 if(!m.embedded)status(c,text::quiet(m.status));
}
Rect noteArea(bool expanded)noexcept{return expanded?NoteExpandedArea:NoteArea;}
Rect notePicker(bool)noexcept{const int y1=t::ScreenHeight-NoteToolRow-t::S2;return {70,y1-ib::reader::PenPanelCompactHeight,70+ib::reader::PenPanelWidth,y1};}
void note(Canvas& c,Targets& tg,const Note& m){
 namespace R=ib::reader;const int top=m.expanded?NoteExpandedTop:NoteSheetTop;const Rect area=noteArea(m.expanded);
 contextSheet(c,top,t::NoteAccent);
 // Header tools, right to left: close, delete, all notes, star, expand.
 const int hy=top+20;int right=1550+t::S3;
 iconButton(c,tg,NoteDone,{right-t::MinTarget,hy,right,hy+t::MinTarget},Icon::Close,{},36);right-=t::MinTarget;
 Rect del{right-(m.confirmDelete?(m.hasHighlight?500:260):t::MinTarget),m.confirmDelete?hy+12:hy,right,m.confirmDelete?hy+t::MinTarget-12:hy+t::MinTarget};
 if(m.confirmDelete)button(c,tg,NoteDelete,del,m.hasHighlight?"Delete note and highlight?":"Delete note?",ButtonKind::Destructive);else iconButton(c,tg,NoteDelete,del,Icon::Trash,{false,m.busy},36);
 right=del.x0;iconButton(c,tg,NoteBack,{right-t::MinTarget,hy,right,hy+t::MinTarget},Icon::Notes,{},36);right-=t::MinTarget;
 if(m.favorites){iconButton(c,tg,NoteFavorite,{right-t::MinTarget,hy,right,hy+t::MinTarget},m.favorite?Icon::StarFilled:Icon::Star,{m.favorite,false,t::PopYellow},36);right-=t::MinTarget;}
 iconButton(c,tg,NoteExpand,{right-t::MinTarget,hy,right,hy+t::MinTarget},m.expanded?Icon::ChevronDown:Icon::ChevronUp,{false,m.busy},36);right-=t::MinTarget;
 iconButton(c,tg,NoteTom,{right-t::MinTarget,hy,right,hy+t::MinTarget},Icon::TomWrite,{false,m.busy||m.playing},36);right-=t::MinTarget;
 iconButton(c,tg,NoteSelectInk,{right-t::MinTarget,hy,right,hy+t::MinTarget},Icon::NativeSelection,{m.selecting,m.busy||m.playing},36);right-=t::MinTarget;
 sheetTitle(c,tg,top,NoteHeader,"Note",m.context,m.unresolved?-1:NoteSource,right); // the page (and passage, if any) the note belongs to
 // The passage, with its highlight color as the rule.
 if(m.hasExcerpt&&!m.excerpt.empty())sheetQuote(c,{70,top+NoteHeader,1550,area.y0-8},"\xE2\x80\x9C"+m.excerpt+"\xE2\x80\x9D",t::Ink[m.color%8]);
 // Paper. Typed text (saved Tom answers, older typed notes) stays readable above the ink.
 if(!m.text.empty()){const Font tf{Face::Serif,theme::Body.size};auto lines=text::wrap(tf,m.text,float(width(area)-t::S4));int rows=std::max(1,(height(area)-40)/50);unsigned first=std::min<unsigned>(m.textOffset,unsigned(std::max(0,int(lines.size())-1)));
  for(int i=0;i<rows&&first+unsigned(i)<lines.size();++i)text::draw(c,tf,float(area.x0+t::S2),float(area.y0+40+i*50),lines[first+unsigned(i)],t::InkPrimary);}
 else{ruledPaper(c,area);if(m.editable&&!m.busy&&m.empty)text::draw(c,{Face::SerifItalic,theme::Body.size},float(area.x0+t::S3),float(area.y0+60),"Write a note\xE2\x80\xA6",t::InkTertiary);}
 if(!m.editable&&!m.busy)text::centeredLine(c,{Face::SerifItalic,theme::Label.size},{area.x0,area.y1-80,area.x1,area.y1-20},"This layer is hidden or locked \xE2\x80\x94 open Layers to write",t::InkTertiary);
 // Footer: pen, undo, redo, keyboard, scroll; the save state; Done.
 const int ry=t::ScreenHeight-NoteToolRow;divider(c,70,1550,ry-8);
 auto tool=[&](int id,int k,bool selected,bool enabled,auto&& draw){Rect cell{70+k*t::MinTarget,ry+4,70+(k+1)*t::MinTarget,ry+4+t::MinTarget};if(selected)c.fillRound(centered(cell,80,80),t::RadiusMedium,t::ToolSelected);
  draw(centered(cell,40,40),selected?t::OnSelected:enabled?t::InkPrimary:t::Disabled);tg.add(id,cell);};
 tool(NoteInk,0,m.picker,m.editable,[&](Rect b,std::uint32_t){penGlyph(c,m.eraser?Instrument::Eraser:m.pen,outset(b,9),0,m.picker,!m.editable);});
 tool(NoteUndo,1,false,m.canUndo&&!m.busy,[&](Rect b,std::uint32_t col){icon(c,Icon::NativeUndo,b,col);});
 tool(NoteRedo,2,false,m.canRedo&&!m.busy,[&](Rect b,std::uint32_t col){icon(c,Icon::NativeRedo,b,col);});
 tool(NoteType,3,false,!m.busy,[&](Rect b,std::uint32_t col){icon(c,Icon::Keyboard,b,col);}); // typing stays available, quietly
 tool(NoteUp,4,false,!m.busy,[&](Rect b,std::uint32_t col){icon(c,Icon::ChevronUp,b,col);});
 tool(NoteDown,5,false,!m.busy,[&](Rect b,std::uint32_t col){icon(c,Icon::ChevronDown,b,col);});
 const Rect done{1550-172,ry+12,1550,ry+t::MinTarget-4};primaryAction(c,tg,NoteSave,done,"Done",Icon::Count,false);
 if(m.playing){textAction(c,tg,NoteFinishInk,{70+6*t::MinTarget+t::S3,ry+4,1040,ry+4+t::MinTarget},"Finish instantly");textAction(c,tg,NoteInkSpeed,{1060,ry+4,done.x0-16,ry+4+t::MinTarget},m.playbackSpeed==0?"Quick":m.playbackSpeed==1?"Normal":"Deliberate");}
 else if(m.retryInk){textAction(c,tg,NoteRetryInk,{70+6*t::MinTarget+t::S3,ry+4,done.x0-16,ry+4+t::MinTarget},m.confirmInkRetry?"Confirm retry":"Retry ink",false,m.busy);}
 else{const Font sf{Face::Text,theme::Meta.size};Rect st{70+6*t::MinTarget+t::S3,ry+4,done.x0-t::S3,ry+4+t::MinTarget};std::string s=!m.status.empty()?text::quiet(m.status):m.busy?"Saving\xE2\x80\xA6":"Saved on this tablet";
  text::centeredLine(c,sf,st,text::fit(sf,s,float(width(st))),m.status.empty()?t::InkSecondary:t::InkPrimary,Align::Left);} /* only real problems replace the save state */
 if(m.inkActions){const Rect popup{780,NoteArea.y1-300,1550,NoteArea.y1-12};popover(c,popup);textAction(c,tg,NoteInkSmaller,{800,popup.y0+8,1150,popup.y0+88},"Smaller",false,!m.hasInkSelection);textAction(c,tg,NoteInkLarger,{1160,popup.y0+8,1530,popup.y0+88},"Larger",false,!m.hasInkSelection);textAction(c,tg,NoteInkColor,{800,popup.y0+96,1150,popup.y0+176},"Change color",false,!m.hasInkSelection);textAction(c,tg,NoteInkDelete,{1160,popup.y0+96,1530,popup.y0+176},"Delete ink",false,!m.hasInkSelection);textAction(c,tg,NoteExportInk,{800,popup.y0+184,1530,popup.y0+264},"Export ink as SVG",false,m.busy);}
 if(m.picker){R::Tools tools;tools.pen=m.pen;tools.width=R::Width(std::min(m.width,2u));tools.colors=false;tools.ink=t::InkPrimary;tools.eraser=m.eraser;tools.busy=!m.editable;R::penPanel(c,tg,tools,notePicker(m.expanded));auto p=notePicker(m.expanded);{const Rect e=hitArea({p.x1-176,p.y0,p.x1-88,p.y0+80});if(m.eraser)c.fillRound(centered(e,72,72),t::RadiusMedium,t::ToolSelected);penGlyph(c,Instrument::Eraser,centered(e,56,56),0,m.eraser,!m.editable);if(m.editable)tg.add(NoteEraser,e);}}
}
void highlight(Canvas& c,Targets& tg,const HighlightDetail& m){
 // The same lower sheet as Note and Tom: square, one ink rule, close in the same place.
 const int top=layout::SheetTop,x0=layout::SheetMargin,x1=t::ScreenWidth-layout::SheetMargin;contextSheet(c,top,t::Ink[m.color%t::Ink.size()]);
 iconButton(c,tg,HlClose,{x1+t::S3-t::MinTarget,top+20,x1+t::S3,top+20+t::MinTarget},Icon::Close,{},36);
 sheetTitle(c,tg,top,layout::SheetHeader,"Highlight",{},-1,x1+t::S3-t::MinTarget);
 int y=quote(c,{x0,top+layout::SheetHeader,x1,0},m.source,m.excerpt,3)+t::S4;
 for(unsigned k=0;k<t::HighlightOrder.size();++k){unsigned i=t::HighlightOrder[k];swatch(c,tg,HlColorBase+int(i),{x0+int(k)*100,y,x0+int(k+1)*100,y+100},t::Ink[i],m.color==i,m.busy,56);}
 iconButton(c,tg,HlUndo,{x1-2*t::MinTarget,y,x1-t::MinTarget,y+100},Icon::Undo,{false,m.busy});iconButton(c,tg,HlRedo,{x1-t::MinTarget,y,x1,y+100},Icon::Redo,{false,m.busy});
 y+=100+t::S4;
 button(c,tg,HlAttachNote,{x0,y,x0+440,y+92},"Write a note",ButtonKind::Primary,m.busy,Icon::Note);
 button(c,tg,HlSource,{x0+460,y,x0+800,y+92},"Go to source",ButtonKind::Secondary,m.busy,Icon::Link);
 button(c,tg,HlDelete,{x1-360,y,x1,y+92},m.confirmDelete?"Confirm remove":"Remove",ButtonKind::Destructive,m.busy,Icon::Trash);
 status(c,text::quiet(m.status));
}
}
