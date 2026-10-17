// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/tom_screen.h"
#include "ib/reader_screen.h"
#include "ib/handwriting.h"
#include "ib/type.h"
#include <algorithm>
#include <array>
namespace ib::tom {
namespace t=theme;
namespace {
const Font Meta{Face::Text,float(t::Meta.size)},Caption{Face::Text,float(t::Caption.size)};
constexpr int Margin=70,Right=1550;
// Tom's hand: Caveat in one dark ink, with small deterministic variations per word (baseline,
// size, spacing) so a reply reads as written rather than typeset. No colors, no animation.
constexpr std::array<float,3> HandSizes{42,50,60};constexpr std::array<int,3> HandLines{64,74,88};
Font answerFont(const Model& m){return m.typed?type::voice(m.handSize):Font{Face::Hand,HandSizes[std::min(m.handSize,2u)]};}
int lineHeight(const Model& m){return m.typed?type::voiceLine(m.handSize):HandLines[std::min(m.handSize,2u)];}
std::vector<std::string> answerWrap(const Model& m,const std::string& s){return text::wrap(answerFont(m),s,float(Right-Margin-60-t::MinTarget));}
unsigned mixHash(unsigned h){h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;return h;}
// Word placement on a hand line (small per-word baseline, size and spacing changes so a reply reads as written),
// shared by drawing and by measuring how much ink a line takes.
template<class F>void handWords(const Font& Hand,const std::string& line,unsigned seed,F each){
 std::size_t at=0;unsigned n=0;const float space=text::measure(Hand," ");float x=0;
 while(at<=line.size()){std::size_t end=line.find(' ',at);if(end==std::string::npos)end=line.size();const std::string word=line.substr(at,end-at);
  if(!word.empty()){const unsigned h=mixHash(seed*131u+n++);const float dy=float(int(h%5u)-2)*0.9f,scale=1.f+float(int((h>>8)%7u)-3)*0.012f;
   const auto& w=hand::word(word,Hand.size*scale);each(w,x,dy);x+=w.advance+space*(0.92f+float((h>>16)%9u)*0.02f);}
  at=end+1;}
}
float lineInk(const Font& Hand,const std::string& line,unsigned seed){float ink=0;handWords(Hand,line,seed,[&](const hand::Word& w,float,float){ink+=w.ink;});return ink;}
// While the reply streams, only words already followed by a space or line break are written: a word still arriving
// might change, and what the pen has drawn never changes.
std::string writable(const Model& m){if(!m.streaming)return m.answer;const auto cut=m.answer.find_last_of(" \n");return cut==std::string::npos?std::string{}:m.answer.substr(0,cut);}
}
int answerLines(const Model& m){return int(answerWrap(m,m.answer).size());}
namespace {Rect answerArea(const Model& m){return m.inlineComposer?Rect{70,680,1550,1320}:WritingArea;}}
int answerRows(const Model& m){const Rect a=answerArea(m);return std::max(1,(height(a)-24)/lineHeight(m));}
HandProgress handProgress(const Model& m,float ink){HandProgress p;if(m.typed)return p;const Font f=answerFont(m);const auto lines=answerWrap(m,writable(m));
 for(std::size_t i=0;i<lines.size();++i){const float li=lineInk(f,lines[i],unsigned(i)+1);if(ink>=p.total)p.line=int(i);p.total+=li;}return p;}
Rect answerLineBox(const Model& m,unsigned line){const Rect a=answerArea(m);const int lh=lineHeight(m);if(line<m.answerScroll)return {};int row=int(line-m.answerScroll);if(row>=answerRows(m))return {};
 const int base=a.y0+56+row*lh;return {a.x0,base-lh+8,a.x1-t::MinTarget,std::min(a.y1,base+22)};}
int sheetTop(const Model& m){return (m.inlineComposer||(m.expanded&&m.composing))&&!m.history?ExpandedSheetTop:SheetTop;}
void paint(Canvas& c,Targets& tg,const Model& m){
 // Native lower sheet: the book stays in view above it; one ink rule, opaque paper.
 const int top=sheetTop(m);contextSheet(c,top,t::TomAccent);
 const Rect head{0,top,t::ScreenWidth,top+HeaderHeight};
 // Header tools, right to left: close, history, expand (while writing).
 int right=Right+t::S3;auto tool=[&](int id,Icon ic,bool on=false){iconButton(c,tg,id,{right-t::MinTarget,head.y0+20,right,head.y0+20+t::MinTarget},ic,{on},36);right-=t::MinTarget;};
 tool(Close,Icon::Close);tool(History,Icon::Clock,m.history);if(!m.history&&m.composing&&!m.historyTurn){if(!m.inlineComposer)tool(Expand,m.expanded?Icon::ChevronDown:Icon::ChevronUp);else tool(Half,m.lowerHalf?Icon::ChevronUp:Icon::ChevronDown);tool(InkTools,Icon::Handwrite,m.picker);}
 sheetTitle(c,tg,top,HeaderHeight,"Tom",m.contextLine,m.hasSource?Source:-1,right);
 int y=head.y1;
 if(m.history){
  // Saved conversations (left) and the questions of this conversation (right), four rows each.
  text::draw(c,Meta,float(Margin),float(y+40),"Conversations",t::InkSecondary);text::draw(c,Meta,float(830),float(y+40),"Questions in this conversation",t::InkSecondary);y+=64;
  constexpr int RowH=112;
  for(unsigned i=0;i<HistoryRows&&m.threadOffset+i<m.threads.size();++i){Rect r{Margin,y+int(i)*RowH,780,y+int(i)*RowH+RowH-8};divider(c,r.x0,r.x1,r.y1);text::centeredLine(c,{Face::Serif,theme::Body.size},Rect{r.x0+t::S2,r.y0+t::S2,r.x1-t::S2,r.y0+56},text::fit({Face::Serif,theme::Body.size},m.threads[m.threadOffset+i].title,float(width(r)-2*t::S2)),t::InkPrimary,Align::Left);text::centeredLine(c,Caption,{r.x0+t::S2,r.y0+56,r.x1-t::S2,r.y1-t::S2},m.threads[m.threadOffset+i].metadata,t::InkSecondary,Align::Left);tg.add(ThreadBase+int(m.threadOffset+i),r);}
  for(unsigned i=0;i<HistoryRows&&m.turnOffset+i<m.turns.size();++i){Rect r{830,y+int(i)*RowH,Right,y+int(i)*RowH+RowH-8};divider(c,r.x0,r.x1,r.y1);text::centeredLine(c,{Face::SerifItalic,theme::Body.size},inset(r,t::S2),m.turns[m.turns.size()-1-(m.turnOffset+i)].question,t::InkPrimary,Align::Left);tg.add(TurnBase+int(m.turns.size()-1-(m.turnOffset+i)),r);}
  int by=y+int(HistoryRows)*RowH+t::S2;
  iconButton(c,tg,ThreadsPrev,{Margin,by,Margin+t::MinTarget,by+t::MinTarget},Icon::ChevronUp,{false,m.threadOffset==0});iconButton(c,tg,ThreadsNext,{Margin+t::MinTarget,by,Margin+2*t::MinTarget,by+t::MinTarget},Icon::ChevronDown,{false,m.threadOffset+HistoryRows>=m.threads.size()});
  iconButton(c,tg,TurnsPrev,{830,by,830+t::MinTarget,by+t::MinTarget},Icon::ChevronUp,{false,m.turnOffset==0});iconButton(c,tg,TurnsNext,{830+t::MinTarget,by,830+2*t::MinTarget,by+t::MinTarget},Icon::ChevronDown,{false,m.turnOffset+HistoryRows>=m.turns.size()});
  by+=t::MinTarget+t::S2;
  button(c,tg,HistoryBack,{Margin,by,Margin+380,by+t::MinTarget},"Back to Tom",ButtonKind::Quiet,false,Icon::Back);
  button(c,tg,NewChat,{Margin+400,by,Margin+760,by+t::MinTarget},"New conversation",ButtonKind::Quiet,false,Icon::Plus);
  button(c,tg,HistoryDelete,{Right-420,by,Right,by+t::MinTarget},m.confirmDelete?"Confirm delete":"Delete conversation",m.confirmDelete?ButtonKind::Destructive:ButtonKind::Quiet,false,Icon::Trash);
  if(!m.status.empty())text::draw(c,Caption,float(Margin),float(t::ScreenHeight-26),text::fit(Caption,text::quiet(m.status),float(Right-Margin)),m.failed?t::InkPrimary:t::InkSecondary);
  return;
 }
 const Rect area=m.inlineComposer?WritingArea:m.expanded&&m.composing?ExpandedWritingArea:WritingArea;
 // One line between header and writing: the passage while writing; the recognized question above an answer.
 {const Rect quote{Margin,y,Right,(m.inlineComposer?680:area.y0)-8};
  if(m.showAnswer&&!m.question.empty()){const Font qf{Face::Text,theme::Meta.size};text::centeredLine(c,qf,quote,text::fit(qf,"You asked: "+m.question,float(width(quote))),t::InkSecondary,Align::Left);}
  else if(!m.excerpt.empty()){sheetQuote(c,quote,"\xE2\x80\x9C"+m.excerpt+"\xE2\x80\x9D",t::Ink[4]);if(m.hasSource)tg.add(Source,quote);}}
 if(m.showAnswer){
  // Tom's answer in his hand (or the typed view), on the same paper where the question was written.
  // Only revealed lines are drawn; the canonical text is unchanged by presentation.
  const auto response=answerArea(m);const int atop=response.y0,bottom=response.y1,lh=lineHeight(m);const Font f=answerFont(m);
  const bool pen=!m.typed&&!m.answer.empty();
  auto lines=answerWrap(m,m.answer.empty()?"\xE2\x80\xA6":pen?writable(m):m.answer);if(lines.empty())lines.push_back("");const int rows=answerRows(m);unsigned first=std::min<unsigned>(m.answerScroll,unsigned(std::max(0,int(lines.size())-1)));
  if(m.nativeReply){ruledPaper(c,response);}else if(!pen){const unsigned shown=m.answer.empty()?unsigned(lines.size()):std::min<unsigned>(m.revealLines,unsigned(lines.size()));
   for(int i=0;i<rows&&first+unsigned(i)<shown;++i){const float bx=float(area.x0+t::S3),by=float(atop+56+i*lh);
    if(m.typed)text::draw(c,f,bx,by,lines[first+unsigned(i)],t::InkPrimary);else text::draw(c,f,bx,by,lines[first+unsigned(i)],t::InkPrimary);}}
  else{ // Tom's pen: everything up to revealInk, the current word stopping mid-stroke.
   float left=m.revealInk;if(left>=0)for(unsigned i=0;i<first&&i<lines.size();++i)left-=lineInk(f,lines[i],i+1);
   for(int i=0;i<rows&&first+unsigned(i)<lines.size();++i){if(m.revealInk>=0&&left<=0)break;const float bx=float(area.x0+t::S3),by=float(atop+56+i*lh);
    handWords(f,lines[first+unsigned(i)],first+unsigned(i)+1,[&](const hand::Word& w,float x,float dy){if(m.revealInk>=0&&left<=0)return;const float used=hand::draw(c,w,bx+x,by+dy,m.revealInk<0?-1.f:left,t::InkPrimary);if(m.revealInk>=0)left-=used;});}}
  if(int(lines.size())>rows){iconButton(c,tg,AnswerUp,{Right-t::MinTarget,atop,Right,atop+t::MinTarget},Icon::ChevronUp,{false,first==0});iconButton(c,tg,AnswerDown,{Right-t::MinTarget,bottom-t::MinTarget,Right,bottom},Icon::ChevronDown,{false,first+unsigned(rows)>=lines.size()});}
 }else{
  ruledPaper(c,area);
  if(!m.question.empty())text::block(c,{Face::Serif,theme::Body.size},{area.x0+t::S3,area.y0+24,area.x1-t::S3,area.y1},m.question,t::InkPrimary,52,20); // a typed question
  else if(m.empty&&!m.historyTurn)text::draw(c,{Face::SerifItalic,theme::Body.size},float(area.x0+t::S3),float(area.y0+60),"Write to Tom\xE2\x80\xA6",t::InkTertiary);}
 if(m.inlineComposer){c.hline(Margin,Right,1340,t::Border,t::Hairline);ruledPaper(c,WritingArea);if(!m.draftQuestion.empty())text::block(c,{Face::Serif,theme::Body.size},{WritingArea.x0+t::S3,WritingArea.y0+24,WritingArea.x1-t::S3,WritingArea.y1},m.draftQuestion,t::InkPrimary,52,12);else if(m.empty)text::draw(c,{Face::SerifItalic,theme::Label.size},float(WritingArea.x0+t::S3),float(WritingArea.y0+56),"Continue the conversation…",t::InkTertiary);}
 // Footer: tools on the left, one status line, the primary action on the right.
 c.hline(Margin,Right,FooterTop,t::Border,t::Hairline);const int fy=FooterTop+8,fh=t::MinTarget;const Rect primary{Right-180,fy+8,Right,fy+fh-8};
 const Font sf{Face::Text,theme::Meta.size};int sx=Margin;std::string status=!m.status.empty()?text::quiet(m.status):m.showAnswer?"":m.disclosure;std::uint32_t sink=m.failed?t::InkPrimary:t::InkSecondary;int sright=primary.x0-t::S3;
 if(m.inlineComposer||!m.historyTurn){iconButton(c,tg,Clear,{sx,fy,sx+t::MinTarget,fy+fh},Icon::Eraser,{false,!m.canClear},36);sx+=t::MinTarget;iconButton(c,tg,Type,{sx,fy,sx+t::MinTarget,fy+fh},Icon::Keyboard,{false,m.busy},36);sx+=t::MinTarget+t::S3;if(m.showAnswer){textAction(c,tg,SaveNote,{sx,fy,sx+210,fy+fh},m.hasNativeOutput?"Open saved note":"Save as note",false,!m.canSaveNote);sx+=210+t::S3;if(m.nativeReply&&m.revealing&&!m.streaming){textAction(c,tg,ShowAll,{sx,fy,sx+220,fy+fh},"Finish writing",false,m.busy);sx+=220+t::S3;}}else if(m.canExplain){textAction(c,tg,Explain,{sx,fy,sx+220,fy+fh},"Explain this");sx+=220+t::S3;}if(m.canStop)textAction(c,tg,Stop,{primary.x0,fy,primary.x1,fy+fh},"Stop",false,false,Align::Right);else primaryAction(c,tg,Send,primary,"Send",Icon::Send,!m.canSend);if(m.canRetry){textAction(c,tg,Retry,{sright-150,fy,sright,fy+fh},m.confirmRetry?"Send again?":"Retry",true,false,Align::Right);sright-=150+t::S3;}}
 else if(m.showAnswer){textAction(c,tg,SaveNote,{sx,fy,sx+250,fy+fh},m.hasNativeOutput?"Open saved note":"Save as note",false,!m.canSaveNote);sx+=250+t::S3;}
 if(!status.empty()&&sright>sx)text::centeredLine(c,sf,{sx,fy,sright,fy+fh},text::fit(sf,status,float(sright-sx)),sink,Align::Left);
}
}
namespace ib {
namespace {
constexpr std::array<std::string_view,3> Letters{"qwertyuiop","asdfghjkl","zxcvbnm"};
constexpr std::array<std::string_view,3> Symbols{"1234567890","-/:;()&@\"","#.,?!'%"};
}
char32_t keyCharacter(int id,bool)noexcept{return id>=KeyBase&&id<KeyBase+128?char32_t(id-KeyBase):0;}
void keyboard(Canvas& c,Targets& tg,const KeyboardModel& m,Rect a){
 namespace t=theme;c.fill(a,t::Surface);c.hline(a.x0,a.x1,a.y0,t::InkPrimary,t::Outline);
 // Field
 Rect field{t::Gutter,a.y0+t::S3,a.x1-t::Gutter,a.y0+170};c.fillRound(field,t::RadiusMedium,t::SurfaceSunken);c.strokeRound(field,t::RadiusMedium,float(t::Outline),t::InkPrimary);
 if(!m.value.empty()){Rect clear{field.x1-t::MinTarget,field.y0+60,field.x1,field.y1};icon(c,Icon::Close,centered(clear,32,32),t::InkSecondary);tg.add(KeyClear,clear);}
 text::draw(c,{Face::Text,float(t::Caption.size)},float(field.x0+t::S3),float(field.y0+34),m.title,t::InkSecondary);
 std::string shown=m.value.empty()?m.hint:m.value+"|";
 auto lines=text::wrap({Face::Serif,theme::Body.size},shown,float(width(field)-t::S5),2);
 if(lines.size()==2&&text::wrap({Face::Serif,theme::Body.size},shown,float(width(field)-t::S5)).size()>2)lines={"\xE2\x80\xA6"+lines.back()};
 for(std::size_t i=0;i<lines.size();++i)text::draw(c,{Face::Serif,theme::Body.size},float(field.x0+t::S3),float(field.y0+84+int(i)*46),lines[i],m.value.empty()?t::InkTertiary:t::InkPrimary);
 // Keys
 int ky=field.y1+t::S3;const int kh=156,kw=150,gap=10;
 const auto& rows=m.symbols?Symbols:Letters;
 auto key=[&](int id,Rect r,std::string_view label,bool dark=false,Icon i=Icon::Count){Rect v=inset(r,gap/2);c.fillRound(v,t::RadiusMedium,dark?t::PopCyan:t::Surface);c.strokeRound(v,t::RadiusMedium,float(t::Outline),t::InkPrimary);
  if(i!=Icon::Count)icon(c,i,centered(v,40,40),t::InkPrimary);else text::centeredLine(c,{Face::Text,float(label.size()>1?theme::Label.size:theme::Subhead.size)},v,label,t::InkPrimary);tg.add(id,r);};
 for(int row=0;row<3;++row){auto keys=rows[std::size_t(row)];int n=int(keys.size());int x0=(a.x0+a.x1-n*kw)/2;
  if(row==2){key(m.symbols?KeySymbols:KeyShift,{t::Gutter,ky+row*kh,x0-gap,ky+(row+1)*kh},m.symbols?"abc":"",m.shift,m.symbols?Icon::Count:Icon::ChevronUp);key(KeyBackspace,{x0+n*kw+gap,ky+row*kh,a.x1-t::Gutter,ky+(row+1)*kh},"",false,Icon::Back);}
  for(int i=0;i<n;++i){char ch=keys[std::size_t(i)];if(m.shift&&!m.symbols)ch=char(std::toupper(static_cast<unsigned char>(ch)));char s[2]{ch,0};key(KeyBase+static_cast<unsigned char>(ch),{x0+i*kw,ky+row*kh,x0+(i+1)*kw,ky+(row+1)*kh},s);}}
 int by=ky+3*kh;
 key(KeySymbols,{t::Gutter,by,t::Gutter+220,by+kh},m.symbols?"abc":"123",true);
 int spaceEnd=a.x1-t::Gutter-560;if(m.paste){key(KeyPaste,{spaceEnd-240,by,spaceEnd,by+kh},"Paste",true);spaceEnd-=240;}
 key(KeySpace,{t::Gutter+220,by,spaceEnd,by+kh},"space");
 key(m.multiline?KeyReturn:KeyCancel,{a.x1-t::Gutter-560,by,a.x1-t::Gutter-300,by+kh},m.multiline?"return":"Cancel",true);
 Rect done{a.x1-t::Gutter-300,by,a.x1-t::Gutter,by+kh};c.fillRound(inset(done,gap/2),t::RadiusMedium,t::GoAccent);c.strokeRound(inset(done,gap/2),t::RadiusMedium,float(t::Outline),t::InkPrimary);text::centeredLine(c,{Face::TextSemibold,theme::Body.size},inset(done,gap/2),"Done",t::onPop(t::GoAccent));tg.add(KeyDone,done);
 if(m.multiline){Rect cancel{field.x1-200,field.y0,field.x1,field.y0+60};text::centeredLine(c,{Face::Text,float(t::Caption.size)},cancel,"Cancel",t::InkSecondary,Align::Right);tg.add(KeyCancel,hitArea(cancel));}
}
}
