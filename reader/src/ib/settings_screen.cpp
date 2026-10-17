// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/settings_screen.h"
#include "ib/text.h"
#include "ib/theme.h"
#include "ib/type.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
namespace ib::settings {namespace {
namespace t=ib::theme;
const Font Title{Face::TextSemibold,theme::Heading.size},Row{Face::Text,theme::Label.size},Value{Face::Text,theme::Label.size},Group{Face::SansSemibold,theme::Caption.size,2.5f},Note{Face::Text,theme::Meta.size},Nav{Face::Text,theme::Label.size};
constexpr int RowH=108;
// Index-tab colours for the page list (General, Wi-Fi, Battery, Storage, Security, Accessibility, Reading, Tom, About):
// the panel's clean colours, Tom blue as everywhere, About in ink.
constexpr std::array<std::uint32_t,unsigned(Page::Count)> PageColor{t::PopCyan,t::PopBlue,t::PopYellow,t::PopGreen,t::PopRed,t::PopMagenta,t::PopCyan,t::TomAccent,t::InkPrimary};
// Pane cursor: group labels, rows (label left, value or control right, hairline under), captions.
struct Pane {
 Canvas& c;Targets& tg;int x0,x1,y;
 // Group labels: small spaced capitals in print, then a dotted rule to the edge, as a form's section heads.
 void group(std::string_view label){y+=t::S5;std::string caps(label);for(auto& ch:caps)ch=char(std::toupper(static_cast<unsigned char>(ch)));
  const float w=text::draw(c,Group,float(x0),float(y+28+text::capHeight(Group)/2),caps,t::InkPrimary);divider(c,x0+int(w)+t::S2,x1,y+28);y+=56;}
 Rect row(std::string_view label){Rect r{x0,y,x1,y+RowH};text::centeredLine(c,Row,{x0,y,x1-420,y+RowH},label,t::InkPrimary,Align::Left);divider(c,x0,x1,y+RowH);y+=RowH;return r;}
 void value(std::string_view label,std::string_view v){Rect r=row(label);text::centeredLine(c,Value,{r.x0+380,r.y0,r.x1,r.y1},v,t::InkPrimary,Align::Right);}
 void toggleRow(std::string_view label,int id,bool on){Rect r=row(label);toggle(c,tg,id,{r.x1-120,r.y0,r.x1,r.y1},on);}
 // Native choice cells: outlined, the chosen one black. ids[i]<0: no target (already chosen, or no action).
 void choices(std::string_view label,const std::vector<std::string>& options,std::size_t chosen,const std::vector<int>& ids){
  Rect r=row(label);const Font f{Face::Text,theme::Control.size};int x=r.x1;
  for(int i=int(options.size())-1;i>=0;--i){const int w=std::max(120,int(text::span(f,options[std::size_t(i)]))+t::S4*2);Rect cell{x-w,r.y0+18,x,r.y1-18};x=cell.x0-8;const bool on=std::size_t(i)==chosen;
   c.fillRound(cell,t::RadiusMedium,on?t::ToolSelected:t::Surface);c.strokeRound(cell,t::RadiusMedium,float(t::Outline),t::InkPrimary);text::centeredLine(c,f,cell,options[std::size_t(i)],on?t::OnSelected:t::InkPrimary,Align::Center);
   if(ids[std::size_t(i)]>=0)tg.add(ids[std::size_t(i)],hitArea(cell));}}
 void note(std::string_view s,int lines=2){text::block(c,Note,{x0,y+t::S3,x1,y+t::S3+lines*40+8},s,t::InkPrimary,40,lines);y+=t::S3+lines*40+t::S2;}
};
}
const char* pageName(Page p)noexcept{static constexpr std::array<const char*,unsigned(Page::Count)> N{"General","Wi-Fi","Battery","Storage","Security","Accessibility","Reading","Tom","About"};return unsigned(p)<N.size()?N[unsigned(p)]:"";}
std::string size(std::int64_t bytes){char out[32];if(bytes<0)return {};if(bytes<1000000000)std::snprintf(out,sizeof out,"%lld MB",static_cast<long long>((bytes+500000)/1000000));else std::snprintf(out,sizeof out,"%.1f GB",double(bytes)/1e9);return out;}
void paint(Canvas& c,Targets& tg,const Model& m){
 // Left: the title and the page list (the chosen page black, as in the mockup); a hairline divides it from the pane.
 {const Font h=type::ScriptHeading;swash(c,h,float(NavRect.x0),PaneRect.y0+(90+text::capHeight(h))/2.f,"Settings",t::SectionColor[7]);}
 for(int y=PaneRect.y0;y+2<=PaneRect.y1;y+=6)c.fill({419,y,421,y+2},t::InkPrimary);
 for(unsigned i=0;i<unsigned(Page::Count);++i){Rect r{NavRect.x0,NavRect.y0+int(i)*100,NavRect.x1,NavRect.y0+int(i)*100+100};const bool on=Page(i)==m.page;
  // Every page has an index tab in its colour at the list's edge; the open page is an ink row.
  if(PageColor[i]!=t::InkPrimary){const Rect tab{r.x0,r.y0+18,r.x0+14,r.y1-18};c.fill(tab,PageColor[i]);c.hairline(tab,t::InkPrimary,t::Outline);}
  if(on)c.fillRound({r.x0+26,r.y0+8,r.x1-12,r.y1-8},t::RadiusMedium,t::ToolSelected);
  text::centeredLine(c,Nav,{r.x0+46,r.y0,r.x1-24,r.y1},pageName(Page(i)),on?t::OnSelected:t::InkPrimary,Align::Left);tg.add(PageBase+int(i),r);}
 Pane p{c,tg,PaneRect.x0,PaneRect.x1,PaneRect.y0};
 text::centeredLine(c,Title,{p.x0,p.y,p.x1,p.y+90},pageName(m.page),t::InkPrimary,Align::Left);p.y+=90;
 switch(m.page){
 case Page::Reading:{
  p.group("Page and tools");
  p.choices("Toolbar position",{"Left","Right"},m.leftHanded?1:0,{m.leftHanded?LeftHanded:-1,m.leftHanded?-1:LeftHanded});
  p.toggleRow("Toolbar handle while reading",ToolHandle,m.toolHandle);
  p.toggleRow("Show reading progress",ReadingProgress,m.progress);
  if(m.reduceMotion)p.value("Page turn effects","Off (Reduce motion)");else p.toggleRow("Page turn effects",Effects,m.effects);
  p.note("Snapped highlights follow the words your highlighter crosses; turn it off to keep freehand highlighter marks. Page turn effects add a short settle after page changes; off keeps refreshes minimal.",3);
  p.group("Power");
  p.toggleRow("Sleep when cover closes",SleepCover,m.sleepCover);
  {Rect r=p.row("Auto sleep");const Font f{Face::Text,theme::Control.size};const int n=int(m.idleChoices.size());int x=r.x1;
   for(int i=n-1;i>=0;--i){const int minutes=m.idleChoices[std::size_t(i)];const std::string label=minutes==0?std::string("Never"):std::to_string(minutes)+" min";const int w=std::max(96,int(text::span(f,label))+t::S3*2);
    Rect cell{x-w,r.y0+18,x,r.y1-18};x=cell.x0-6;const bool on=minutes==m.sleepIdle;c.fillRound(cell,t::RadiusMedium,on?t::ToolSelected:t::Surface);c.strokeRound(cell,t::RadiusMedium,float(t::Outline),t::InkPrimary);
    text::centeredLine(c,f,cell,label,on?t::OnSelected:t::InkPrimary,Align::Center);tg.add(IdleBase+i,hitArea(cell));}}
  p.note("Inkie saves your work and lets the tablet sleep. Open the cover or press power to wake.");
  break;}
 case Page::General:
  p.group("Date and time");{Rect r=p.row("Time zone");button(c,tg,TimeZone,{r.x1-580,r.y0+14,r.x1,r.y1-14},text::fit({Face::Text,theme::Control.size},m.timeZone,540),ButtonKind::Secondary);}
  p.note("Choose the time zone Inkie uses for its clock. Daylight saving time follows the chosen zone.",2);
  if(m.userEnded){p.group("Session");p.note("This session ends with the power button.",1);}
  else{p.group("Session");Rect r=p.row("Return to the reMarkable home screen");button(c,tg,Leave,{r.x1-300,r.y0+14,r.x1,r.y1-14},"Leave",ButtonKind::Secondary);
   p.note("Your books, notes and handwriting stay saved in Inkie.",1);}
  break;
 case Page::WiFi:
  p.group("Connection");
  if(m.wifiLink>=0)p.value("Status",m.wifiLink?"Connected":"Not connected");
  {Rect r=p.row("Networks");button(c,tg,Wifi,{r.x1-300,r.y0+14,r.x1,r.y1-14},"Choose",ButtonKind::Secondary,false,Icon::Wifi);}
  p.note("Networks joined here are shared with the reMarkable's own Wi-Fi settings.",1);
  break;
 case Page::Battery:
  p.group("Battery");
  if(m.battery>=0)p.value("Charge",std::to_string(m.battery)+"%");
  if(!m.charge.empty())p.value("Status",m.charge);
  if(m.externalPower>=0)p.value("Power",m.externalPower?"Charger connected":"On battery");
  if(m.battery<0&&m.charge.empty())p.note("The battery gauge isn't readable right now.",1);
  else p.note("Read from the tablet's fuel gauge about once a minute. Inkie warns once at 10%.",1);
  break;
 case Page::Storage:
  p.group("Tablet storage");
  if(m.storageTotal>0&&m.storageFree>=0){p.value("Available",size(m.storageFree)+" of "+size(m.storageTotal));
   const Rect bar{p.x0,p.y+t::S3,p.x1,p.y+t::S3+20};c.hairline(bar,t::InkPrimary,2);const double used=1.0-double(m.storageFree)/double(m.storageTotal);
   const int fill=int(double(width(bar)-8)*std::clamp(used,0.0,1.0));if(fill>0)c.fill({bar.x0+4,bar.y0+4,bar.x0+4+fill,bar.y1-4},t::InkPrimary);p.y=bar.y1+t::S2;}
  else p.note("Storage isn't readable right now.",1);
  p.group("Inkie");
  if(m.documents>=0)p.value("Books and documents",std::to_string(m.documents));
  if(m.notes>=0)p.value("Notes",std::to_string(m.notes));
  break;
 case Page::Security:{
  p.group("Passcode");
  {Rect r=p.row("Passcode lock");text::centeredLine(c,Value,{r.x0+380,r.y0,r.x1-330,r.y1},m.passcode?"On":"Off",t::InkSecondary,Align::Right);
   button(c,tg,PasscodeSet,{r.x1-300,r.y0+14,r.x1,r.y1-14},m.passcode?"Change":"Set up",ButtonKind::Secondary);}
  if(m.passcode){Rect r=p.row("Turn off the passcode");button(c,tg,PasscodeOff,{r.x1-300,r.y0+14,r.x1,r.y1-14},"Turn off",ButtonKind::Secondary);}
  p.note("Inkie asks for the passcode when it starts, before the tablet sleeps and when it wakes. It locks the screen; it does not encrypt your files. The reMarkable's own passcode still protects the rest of the tablet.",3);
  p.group("Companion");
  if(m.companion<0)p.value("Companion","Off on this tablet");
  else{static constexpr std::array<const char*,4> S{"Not paired","Pairing","Paired","Needs attention"};Rect r=p.row("Companion");text::centeredLine(c,Value,{r.x0+380,r.y0,r.x1-330,r.y1},S[std::size_t(std::clamp(m.companion,0,3))],t::InkSecondary,Align::Right);
   button(c,tg,CompanionManage,{r.x1-300,r.y0+14,r.x1,r.y1-14},"Manage",ButtonKind::Secondary);}
  p.group("What Tom sends");
  p.note("Only when you send a question: your handwritten question, the book's title, the page and passage you are asking about, and the earlier questions and replies in that conversation. Your library, notes and other handwriting stay on this tablet.",3);
  break;}
 case Page::Accessibility:
  p.group("Display");
  p.toggleRow("High contrast",HighContrast,m.highContrast);
  p.note("Darker secondary text, lines and controls throughout Inkie.",1);
  p.group("Motion");
  p.toggleRow("Reduce motion",ReduceMotion,m.reduceMotion);
  p.note("Tom's replies appear all at once instead of line by line, and page turn effects stay off.",1);
  // Last on the page, so adding it moved no existing row.
  p.group("Text");
  p.toggleRow("Plain text instead of cursive",PlainText,m.plainText);
  p.note("Inkie writes everything in cursive. This shows labels, headings and Tom's typed replies in Source Sans and Source Serif instead; the colours stay.",2);
  break;
 case Page::Tom:
  p.group("Replies");
  p.choices("Tom replies in",{"Handwriting","Typed text"},m.tomTyped?1:0,{m.tomTyped?TomStyleBase:-1,m.tomTyped?-1:TomStyleBase+1});
  p.choices("Handwriting size",{"Small","Medium","Large"},std::min(m.tomHand,2u),{m.tomHand==0?-1:TomHandBase,m.tomHand==1?-1:TomHandBase+1,m.tomHand==2?-1:TomHandBase+2});
  if(m.reduceMotion)p.value("Write replies line by line","Off (Reduce motion)");else p.toggleRow("Write replies line by line",TomReveal,m.tomReveal);
  p.note("These are how a reply first appears; the controls under a reply still switch it while you read.",1);
  if(m.tomOnline>=0){p.group("Connection");p.value("Tom",m.tomOnline?"Online when Wi-Fi is connected":"Offline on this tablet");}
  break;
 case Page::About:
  p.group("Inkie");
  for(const auto& [k,v]:m.about)p.value(k,v);
  if(!m.firmware.empty()){p.group("Tablet");p.value("Software",m.firmware);}
  break;
 case Page::Count:break;
 }
}
}
