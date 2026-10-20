#include "document_controls.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
namespace ux {namespace {
using D=DocumentControls;constexpr int Grid=ib::sheets::OverviewPageSize;using Rect=reader::pdfview::Rect;
bool sameRect(Rect a,Rect b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
bool samePdf(const reader::pdfview::View& a,const reader::pdfview::View& b){return a.zoom==b.zoom&&a.rotation==b.rotation&&a.pan.x==b.pan.x&&a.pan.y==b.pan.y&&a.crop.has_value()==b.crop.has_value()&&(!a.crop||sameRect(*a.crop,*b.crop));}
bool sameApplied(const D::Snapshot& a,const D::Snapshot& b){return a.binding==b.binding&&a.format==b.format&&a.page==b.page&&a.pages==b.pages&&sameRect(a.sourceBounds,b.sourceBounds)&&(a.format==D::Format::Pdf?samePdf(a.pdf,b.pdf):a.epub==b.epub);}
bool inside(spike::Rect r,int x,int y){return x>=r.x0&&x<r.x1&&y>=r.y0&&y<r.y1;}
std::array<double,4> margins(const D::Snapshot& s,const reader::pdfview::View& view){auto b=s.sourceBounds,c=view.crop.value_or(b);return {c.x-b.x,b.x+b.width-c.x-c.width,c.y-b.y,b.y+b.height-c.y-c.height};}
}
bool DocumentControls::valid(const Snapshot& s)noexcept{try{if(s.binding.document.empty()||s.binding.document.size()>128||s.binding.page.empty()||s.binding.page.size()>128||s.binding.layout.empty()||s.binding.layout.size()>128||!s.binding.epoch||s.pages<1||s.pages>1000||s.page<0||s.page>=s.pages)return false;if(!std::isfinite(s.sourceBounds.x)||!std::isfinite(s.sourceBounds.y)||!std::isfinite(s.sourceBounds.width)||!std::isfinite(s.sourceBounds.height)||s.sourceBounds.width<=0||s.sourceBounds.height<=0)return false;if(s.format==Format::Pdf){(void)reader::pdfview::plan(s.sourceBounds,s.pdf);return true;}return s.format==Format::Epub&&s.epub.fontSize>=6&&s.epub.fontSize<=72&&s.epub.lineWidth>=64&&s.epub.lineWidth<=4094;}catch(...){return false;}}
void DocumentControls::resetPreview(){pdf_=applied_.pdf;epub_=applied_.epub;selectedPage_=applied_.page;selectedBookmark_=-1;firstPage_=(selectedPage_/Grid)*Grid;pending_=false;notice_=Refusal::None;}
bool DocumentControls::open(const Snapshot& s){if(!valid(s))return false;applied_=s;resetPreview();tab_=Tab::View;active_=true;damage_=Panel;return true;}
bool DocumentControls::update(const Snapshot& s){if(!active_||!valid(s))return false;if(s.binding.document==applied_.binding.document&&s.binding.page==applied_.binding.page&&s.binding.layout==applied_.binding.layout&&s.binding.epoch<applied_.binding.epoch)return false;if(s.binding==applied_.binding&&!sameApplied(s,applied_))return false;bool reset=s.binding!=applied_.binding;applied_=s;if(reset)resetPreview();damage_=Panel;return true;}
bool DocumentControls::stagePdf(reader::pdfview::View v){try{auto p=reader::pdfview::plan(applied_.sourceBounds,v);if(p.scale<.1||p.scale>16)throw std::runtime_error("ink-view-scale");for(double n:p.forward.value)if(std::abs(n)>100000)throw std::runtime_error("ink-view-matrix");}catch(...){notice_=Refusal::Invalid;return false;}if(samePdf(v,pdf_))return false;pdf_=v;return true;}
bool DocumentControls::changed()const noexcept{if(tab_==Tab::Pages)return selectedBookmark_>=0||selectedPage_!=applied_.page;if(tab_==Tab::View&&applied_.format==Format::Pdf)return !samePdf(pdf_,applied_.pdf);if(tab_==Tab::Reading&&applied_.format==Format::Epub)return epub_!=applied_.epub;return false;}
DocumentControls::Command DocumentControls::hit(int x,int y,const Snapshot& now){
 if(!active_)return {};Command out{Action::None,applied_.binding,applied_.pdf,applied_.epub,applied_.page,Refusal::None,true};auto refuse=[&](Refusal r){out.refusal=notice_=r;damage_=Panel;return out;};
 namespace S=ib::sheets;const int id=inside(Panel,x,y)?targets_.find(x,y):S::Cancel;
 if(!valid(now)||!sameApplied(now,applied_))return refuse(Refusal::Stale);if(now.contact||pending_)return refuse(Refusal::Busy);
 if(id==S::SettingsClose||id==S::Cancel){resetPreview();active_=false;out.action=Action::Cancel;damage_=Panel;return out;}
 if(id>=S::TabBase&&id<S::TabBase+4){tab_=Tab(id-S::TabBase);notice_=Refusal::None;out.action=Action::PreviewChanged;damage_=Panel;return out;}
 if(now.busy)return refuse(Refusal::Busy);
 if(id==S::Quiet){out.action=Action::Quiet;out.quietEffects=!now.quietEffects;applied_.quietEffects=out.quietEffects;damage_=Panel;return out;}
 const bool layoutTab=(tab_==Tab::View&&applied_.format==Format::Pdf)||(tab_==Tab::Reading&&applied_.format==Format::Epub);
 if(id==S::Apply){if(layoutTab&&now.legacyInkPins)return refuse(Refusal::Pinned);if(!changed())return refuse(Refusal::Unavailable);
  out.action=tab_==Tab::Pages?Action::JumpPage:applied_.format==Format::Pdf?Action::ApplyPdf:Action::ApplyEpub;if(tab_==Tab::Pages){out.targetPage=selectedPage_;out.bookmarkIndex=selectedBookmark_;}else if(applied_.format==Format::Pdf)out.pdf=pdf_;else out.epub=epub_;pending_=true;notice_=Refusal::None;damage_=Panel;return out;}
 bool changedPreview=false;
 if(tab_==Tab::Pages){
  if(id>=S::PageCellBase&&id<S::PageCellBase+Grid){int page=pageForCell(unsigned(id-S::PageCellBase));if(page<0)return refuse(Refusal::Unavailable);selectedPage_=page;selectedBookmark_=-1;changedPreview=true;}
  if(id>=S::BookmarkBase&&id<S::BookmarkBase+S::BookmarkChips){unsigned n=unsigned(id-S::BookmarkBase);if(n>=applied_.bookmarks.size()||applied_.bookmarks[n].first>=applied_.pages)return refuse(Refusal::Unavailable);selectedPage_=applied_.bookmarks[n].first;selectedBookmark_=int(n);firstPage_=(selectedPage_/Grid)*Grid;changedPreview=true;}
  if(id>=S::PageFirst&&id<=S::PageLast){unsigned n=unsigned(id-S::PageFirst);int next=n==0?0:n==1?std::max(0,firstPage_-Grid):n==2?std::min(((applied_.pages-1)/Grid)*Grid,firstPage_+Grid):((applied_.pages-1)/Grid)*Grid;if(next==firstPage_)return refuse(Refusal::Unavailable);firstPage_=next;changedPreview=true;}}
 else if(tab_==Tab::Reading&&applied_.format==Format::Epub){bool control=id==S::FontLess||id==S::FontMore||id==S::LineLess||id==S::LineMore||(id>=S::FamilyBase&&id<S::AlignBase+3);if(control&&now.legacyInkPins)return refuse(Refusal::Pinned);auto next=epub_;
  if(id==S::FontLess)next.fontSize=std::max(6,next.fontSize-2);if(id==S::FontMore)next.fontSize=std::min(72,next.fontSize+2);if(id==S::LineLess)next.lineWidth=std::max(64,next.lineWidth-50);if(id==S::LineMore)next.lineWidth=std::min(4094,next.lineWidth+50);
  if(id>=S::FamilyBase&&id<S::FamilyBase+3)next.typography.family=epub::FontFamily(unsigned(id-S::FamilyBase));if(id>=S::SpacingBase&&id<S::SpacingBase+3)next.typography.spacing=epub::Step(unsigned(id-S::SpacingBase));
  if(id>=S::MarginBase&&id<S::MarginBase+3)next.typography.margins=epub::Step(unsigned(id-S::MarginBase));if(id>=S::AlignBase&&id<S::AlignBase+3)next.typography.alignment=epub::Alignment(unsigned(id-S::AlignBase));
  changedPreview=next!=epub_;if(control&&!changedPreview)return refuse(Refusal::Unavailable);epub_=next;}
 else if(tab_==Tab::View&&applied_.format==Format::Pdf){auto next=pdf_;bool control=false;
  if(id==S::Fit){next.zoom=1;next.pan={};control=true;}
  // Fit width: the page's (cropped, rotated) width fills the screen, starting at its top; text PDFs read larger.
  if(id==S::FitWidth){const auto b=next.crop.value_or(applied_.sourceBounds);const bool turn=next.rotation==90||next.rotation==270;const double w=turn?b.height:b.width,h=turn?b.width:b.height;
   if(w<=0||h<=0)return refuse(Refusal::Invalid);const double fitPage=std::min(reader::pdfview::Width/w,reader::pdfview::Height/h);next.zoom=std::clamp((reader::pdfview::Width/w)/fitPage,1.,4.);next.pan={};
   try{const auto p=reader::pdfview::plan(applied_.sourceBounds,next);next.pan={0,p.maxPanY};}catch(...){return refuse(Refusal::Invalid);}control=true;}
  if(id==S::ZoomOut||id==S::ZoomIn){next.zoom=std::clamp(next.zoom+(id==S::ZoomIn?.25:-.25),1.,4.);auto old=next.pan;next.pan={};try{auto p=reader::pdfview::plan(applied_.sourceBounds,next);next.pan={std::clamp(old.x,-p.maxPanX,p.maxPanX),std::clamp(old.y,-p.maxPanY,p.maxPanY)};}catch(...){return refuse(Refusal::Invalid);}control=true;}
  if(id==S::RotateLeft||id==S::RotateRight){next.rotation=(next.rotation+(id==S::RotateRight?90:270))%360;next.pan={};control=true;}
  if(id==S::CropReset){next.crop.reset();next.pan={};control=true;}
  if(id>=S::CropBase&&id<S::CropBase+8){unsigned edge=unsigned(id-S::CropBase)/2;bool plus=(id-S::CropBase)%2;auto m=margins(applied_,pdf_);double unit=(edge<2?applied_.sourceBounds.width:applied_.sourceBounds.height)*.02;m[edge]=std::max(0.0,m[edge]+(plus?unit:-unit));auto b=applied_.sourceBounds;next.crop=reader::pdfview::Rect{b.x+m[0],b.y+m[2],b.width-m[0]-m[1],b.height-m[2]-m[3]};if(std::all_of(m.begin(),m.end(),[](double v){return v==0;}))next.crop.reset();next.pan={};control=true;}
  if(id>=S::PanBase&&id<S::PanBase+4){unsigned direction=unsigned(id-S::PanBase);auto p=reader::pdfview::plan(applied_.sourceBounds,next);if(direction==0)next.pan.x=std::max(-p.maxPanX,next.pan.x-reader::pdfview::Width/8.);if(direction==3)next.pan.x=std::min(p.maxPanX,next.pan.x+reader::pdfview::Width/8.);if(direction==1)next.pan.y=std::max(-p.maxPanY,next.pan.y-reader::pdfview::Height/8.);if(direction==2)next.pan.y=std::min(p.maxPanY,next.pan.y+reader::pdfview::Height/8.);control=true;}
  if(control&&now.legacyInkPins)return refuse(Refusal::Pinned);if(control){notice_=Refusal::None;changedPreview=stagePdf(next);if(!changedPreview)return refuse(notice_==Refusal::Invalid?Refusal::Invalid:Refusal::Unavailable);}}
 if(changedPreview){notice_=Refusal::None;out.action=Action::PreviewChanged;damage_=Panel;}return out;
}
bool DocumentControls::paint(spike::Surface s)noexcept{
 if(!active_)return true;if(s.width!=1620||s.height!=2160||s.stridePixels<1620||s.pixels.size()<std::size_t(s.stridePixels)*2160)return false;
 try{ib::Canvas c(s);targets_.clear();ib::sheets::Settings m;m.tab=ib::sheets::Tab(unsigned(tab_));m.pdf=applied_.format==Format::Pdf;
  auto mg=margins(applied_,pdf_);for(unsigned e=0;e<4;++e){double total=e<2?applied_.sourceBounds.width:applied_.sourceBounds.height;m.crop[e]=total>0?mg[e]*100/total:0;}
  m.zoom=pdf_.zoom;m.rotation=pdf_.rotation;m.panAvailable=pdf_.zoom>1;m.fontSize=epub_.fontSize;m.lineWidth=epub_.lineWidth;m.family=unsigned(epub_.typography.family);m.spacing=unsigned(epub_.typography.spacing);m.margins=unsigned(epub_.typography.margins);m.alignment=unsigned(epub_.typography.alignment);m.quietEffects=applied_.quietEffects;
  m.bookmarks=applied_.bookmarks;m.firstPage=firstPage_;m.pages=applied_.pages;m.selectedPage=selectedPage_;m.currentPage=applied_.page;m.busy=applied_.busy;m.pinned=applied_.legacyInkPins;m.changed=changed();m.pending=pending_;m.contact=applied_.contact;m.pageImage=pageImage_;
  const bool layoutTab=(tab_==Tab::View&&m.pdf)||(tab_==Tab::Reading&&!m.pdf);
  if(notice_==Refusal::Stale)m.notice="The view changed — reopen settings";
  else if(layoutTab&&(applied_.legacyInkPins||notice_==Refusal::Pinned))m.notice="Handwriting on this book keeps its layout fixed";
  else if(applied_.contact)m.notice="Finish the stroke before changing settings";
  else if(layoutTab&&!m.pdf&&applied_.layoutInk&&!pending_&&!applied_.busy)m.notice="Handwriting stays with the layout it was written in \u2014 Book defaults shows it again";
  else if(pending_)m.notice="Applying…";else if(applied_.busy)m.notice="Waiting for the reader…";
  else if(notice_==Refusal::Invalid)m.notice="That change isn’t available — settings kept";
  else if(layoutTab)m.notice=m.pdf?"Preview only — Apply to use these settings":"Changing text size repaginates the book";
  char location[64];std::snprintf(location,sizeof location,"Page %d of %d",applied_.page+1,applied_.pages);
  m.about={{"Title",applied_.title.empty()?"Untitled":applied_.title},{"Format",m.pdf?"PDF · original pages":"EPUB · reflowed to this screen"},{"Location",location},{"Dictionary","WordNet 3.0 and GCIDE · offline"},{"Typefaces","Caveat, Source Serif 4, Source Sans 3 · SIL Open Font License"}};
  ib::sheets::settings(c,targets_,m);}catch(...){return false;}return true;
}
}
