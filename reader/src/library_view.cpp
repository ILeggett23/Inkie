#include "library_view.h"
#include "ux_paint.h"
#include "ib/sheets.h"
#include <QBuffer>
#include <QImageReader>
#include <QRegularExpression>
#include <algorithm>
#include <fcntl.h>
#include <array>
#include <sys/stat.h>
#include <unistd.h>
namespace ux {
namespace {
namespace t=ib::theme;
using LS=ib::library::Section;
namespace S=ib::settings;
enum Ids:int {MenuImport=900,MenuNotebook,MenuShared,MenuLeave,MenuWifi,SettingEffects=S::Effects,SettingLeave=S::Leave,SettingWifi=S::Wifi,SettingSleepCover=S::SleepCover,SettingToolHandle=S::ToolHandle,SettingLeftHanded=S::LeftHanded,SettingSleepIdleBase=S::IdleBase,SettingPageBase=S::PageBase,HubOffset=3000};
constexpr std::array<int,6> SleepIdleChoices{0,5,10,15,30,60}; /* power::IdleChoices */
std::string utf8(const QString& v){auto b=v.toUtf8();return std::string(b.constData(),std::size_t(b.size()));}
struct Fd {int n{-1};~Fd(){if(n>=0)::close(n);}};
QImage readCover(const std::filesystem::path& root,const QString& id){
 if(!QRegularExpression("^(?:[0-9a-f]{64}|[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})\\z").match(id).hasMatch())return {};
 Fd base{::open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW)};if(base.n<0)return {};
 Fd docs{::openat(base.n,"documents",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW)};if(docs.n<0)return {};
 auto name=id.toLatin1();Fd doc{::openat(docs.n,name.constData(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW)};if(doc.n<0)return {};
 Fd file{::openat(doc.n,"cover.png",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK)};if(file.n<0)return {};
 struct stat st{};if(::fstat(file.n,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>1024*1024)return {};
 QByteArray bytes(static_cast<int>(st.st_size),Qt::Uninitialized);std::size_t done=0;
 while(done<static_cast<std::size_t>(bytes.size())){auto n=::read(file.n,bytes.data()+done,bytes.size()-done);if(n<=0)return {};done+=static_cast<std::size_t>(n);}
 QBuffer buffer(&bytes);if(!buffer.open(QIODevice::ReadOnly))return {};QImageReader reader(&buffer,"PNG");
 auto size=reader.size();if(size.width()<1||size.height()<1||size.width()>1024||size.height()>1024||qint64(size.width())*size.height()>1024*1024)return {};
 auto image=reader.read();if(image.isNull()||image.size()!=size)return {};return image.scaled(covers::Width,covers::Height,Qt::KeepAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
}
int readPercent(const library::Document& d){int pages=std::max(1,d.readingPages?d.readingPages:d.pages);return std::clamp((std::clamp(d.position,0,pages-1)+1)*100/pages,0,100);}
std::string meta(const library::Document& d){
 int pages=std::max(1,d.readingPages?d.readingPages:d.pages);
 if(d.trashed)return "In Trash";
 if(!d.opened)return (d.format=="pdf"?"PDF":"EPUB")+std::string(" \xC2\xB7 ")+std::to_string(pages)+(pages==1?" page":" pages");
 return std::to_string(readPercent(d))+"% read";
}
}
spike::Rect LibraryView::keyboardContent()noexcept{auto k=ib::KeyboardRect;return {k.x0+16,k.y0+24,k.x1-16,k.y1-16};}
spike::Rect LibraryView::menuContent()const noexcept{auto r=create_?ib::library::createMenuRect(2):ib::library::DrawerRect;return {r.x0+16,r.y0+24,r.x1-16,r.y1-16};}
spike::Rect LibraryView::effectContent()const noexcept{return keyboard_?keyboardContent():menu_?menuContent():spike::Rect{};}
void LibraryView::setDocuments(const QVector<library::Document>& docs){all_=docs;rebuild();}
int LibraryView::capacity()const{bool root=section_==Section::MyFiles&&folder_.isEmpty()&&query_.isEmpty()&&!keyboard_;return ib::library::capacity(grid_,root?std::min(int(folders_.size()),ib::library::folderCapacity()):0);}
void LibraryView::rebuild(){
 filtered_.clear();folders_.clear();folderCounts_.clear();
 for(const auto& d:all_)if(!d.trashed&&!d.folder.isEmpty()){if(!folderCounts_.contains(d.folder))folders_.push_back(d.folder);folderCounts_[d.folder]++;}
 std::sort(folders_.begin(),folders_.end(),[](const auto& a,const auto& b){return QString::compare(a,b,Qt::CaseInsensitive)<0;});
 bool searching=!query_.isEmpty();
 for(int i=0;i<all_.size();++i){const auto& d=all_[i];bool match=!searching||d.title.contains(query_,Qt::CaseInsensitive);if(!match)continue;
  switch(section_){
  case Section::MyFiles:if(!d.trashed&&(searching||d.folder==folder_))filtered_.push_back(i);break;
  case Section::Favorites:if(!d.trashed&&favorites_.contains(d.id))filtered_.push_back(i);break;
  case Section::Recents:if(!d.trashed&&d.opened>0)filtered_.push_back(i);break;
  case Section::Trash:if(d.trashed)filtered_.push_back(i);break;
  default:break;}}
 bool byRecent=recent_||section_==Section::Recents;
 std::sort(filtered_.begin(),filtered_.end(),[&](int ai,int bi){const auto&a=all_[ai];const auto&b=all_[bi];if(byRecent&&a.opened!=b.opened)return a.opened>b.opened;int c=QString::compare(a.title,b.title,Qt::CaseInsensitive);return c?c<0:a.id<b.id;});
 offset_=filtered_.isEmpty()?0:std::clamp(offset_,0,((int(filtered_.size())-1)/capacity())*capacity());
 filteredNotes_.clear();
 for(int i=0;i<notes_.size();++i){const auto& n=notes_[i];using F=ib::notes::Filter;bool keep=noteFilter_==F::All||(noteFilter_==F::Linked&&n.linked)||(noteFilter_==F::Pages&&!n.linked)||(noteFilter_==F::Favorites&&n.favorite)||(noteFilter_==F::Tom&&n.tom);if(keep)filteredNotes_.push_back(i);}
 noteOffset_=filteredNotes_.isEmpty()?0:std::clamp(noteOffset_,0,((int(filteredNotes_.size())-1)/ib::notes::HubPageSize)*ib::notes::HubPageSize);
 visibleNotes_.clear();for(int k=0;k<ib::notes::HubPageSize&&noteOffset_+k<filteredNotes_.size();++k)visibleNotes_.push_back(filteredNotes_[noteOffset_+k]);
}
QVector<QString> LibraryView::visibleIds()const{QVector<QString> ids;for(int n=0;n<capacity()&&offset_+n<filtered_.size();++n)ids.push_back(all_[filtered_[offset_+n]].id);return ids;}
std::shared_ptr<const LibraryView::Cover> LibraryView::cover(const std::filesystem::path& root,const QString& id){
 if(auto found=covers_.find(id);found!=covers_.end())return found.value();
 auto image=readCover(root,id);std::shared_ptr<const Cover> out;
 if(!image.isNull()){auto c=std::make_shared<Cover>();c->width=image.width();c->height=image.height();c->pixels.resize(std::size_t(c->width)*c->height);
  for(int y=0;y<c->height;++y){auto line=reinterpret_cast<const QRgb*>(image.constScanLine(y));for(int x=0;x<c->width;++x)c->pixels[std::size_t(y)*c->width+x]=0xff000000u|(line[x]&0xffffffu);}out=c;}
 if(covers_.size()>=covers::CacheLimit)covers_.clear();covers_.insert(id,out);return out;
}
void LibraryView::paint(spike::Surface s,const QVector<library::Document>& docs,const std::filesystem::path& root){
 setDocuments(docs);if(s.width!=1620||s.height!=2160||s.stridePixels<s.width||s.pixels.size()<std::size_t(s.stridePixels)*s.height)return;
 ib::Canvas c(s);targets_.clear();
 ib::library::Model m;m.section=section_;m.grid=grid_;m.sharedAvailable=shared_;m.tomAvailable=tom_;m.notice=utf8(notice_);
 m.searching=keyboard_||!query_.isEmpty();m.query=utf8(query_);m.sortLabel=recent_?"Last opened":"Title";
 switch(section_){case Section::MyFiles:m.heading=folder_.isEmpty()?"My files":utf8(folder_);if(!folder_.isEmpty())m.crumb="My files / "+utf8(folder_);break;
  default:m.heading=ib::library::sectionName(section_);break;}
 if(section_==Section::Recents||section_==Section::Notes||section_==Section::Settings)m.sortLabel="";
 bool atRoot=section_==Section::MyFiles&&folder_.isEmpty()&&!m.searching;
 if(atRoot)for(int i=0;i<std::min(int(folders_.size()),ib::library::folderCapacity());++i)m.folders.push_back({utf8(folders_[i]),folderCounts_.value(folders_[i])});
 std::vector<std::shared_ptr<const Cover>> keep;
 for(int n=0;n<capacity()&&offset_+n<filtered_.size();++n){const auto& d=all_[filtered_[offset_+n]];auto cv=coverSource_?coverSource_(d.id,d.sha256):std::shared_ptr<const Cover>{};if(!cv)cv=cover(root,d.id);keep.push_back(cv);
  ib::library::Document doc;doc.title=utf8(d.title);doc.meta=meta(d);doc.format=utf8(d.format);doc.favorite=favorites_.contains(d.id);if(cv)doc.cover={cv->pixels,cv->width,cv->height,cv->width};m.documents.push_back(std::move(doc));}
 m.page=filtered_.isEmpty()?0:offset_/capacity();m.pages=std::max(1,int((filtered_.size()+capacity()-1)/capacity()));
 if(section_==Section::Notes||section_==Section::Settings){m.pages=1;m.documents.clear();}
 m.drawer=menu_;m.create=create_;m.battery=battery_;m.charging=charging_;m.wifi=wifi_;m.clock=clock_;m.clockZone=clockZone_;
 m.createItems={{MenuNotebook,ib::Icon::Note,"Notebook","Lined, grid, dot or blank pages"}};
 if(userEnded_)m.drawerExtras.push_back({MenuWifi,ib::Icon::Wifi,"Wi-Fi",{}});else m.drawerExtras.push_back({MenuLeave,ib::Icon::Back,"Leave Inkie",{}}); /* D2: a user-ended session is left only with the power button */
 ib::library::base(c,targets_,m);
 if(section_==Section::Notes){
  ib::notes::Hub hub;hub.embedded=true;hub.favorites=true;hub.canCreate=false;hub.filter=noteFilter_;hub.idOffset=HubOffset;
  for(int index:visibleNotes_){const auto& n=notes_[index];ib::notes::Entry e;e.title=utf8(n.title);e.excerpt=utf8(n.excerpt);e.body=utf8(n.body);e.source=utf8(n.source);e.color=t::Ink[n.color%8];e.favorite=n.favorite;e.linked=n.linked;e.handwriting=n.handwriting;e.tom=n.tom;e.unresolved=n.unresolved;hub.entries.push_back(std::move(e));}
  hub.page=noteOffset_/ib::notes::HubPageSize;hub.pages=std::max(1,int((filteredNotes_.size()+ib::notes::HubPageSize-1)/ib::notes::HubPageSize));ib::notes::hub(c,targets_,hub);}
 if(section_==Section::Settings){ /* Settings (mockup 25): the kit draws the pages from real settings and readings */
  S::Model st;st.page=settingsPage_;st.effects=effects_;st.toolHandle=toolHandle_;st.leftHanded=leftHanded_;st.progress=progress_;st.snap=snap_;st.passcode=passcode_;st.companion=companion_;st.highContrast=highContrast_;st.reduceMotion=reduceMotion_;st.plainText=plainText_;st.tomTyped=tomTyped_;st.tomHand=tomHand_;st.tomReveal=tomReveal_;st.tomOnline=tomOnline_;st.sleepCover=sleepCover_;st.sleepIdle=sleepIdle_;st.idleChoices.assign(SleepIdleChoices.begin(),SleepIdleChoices.end());st.userEnded=userEnded_;st.timeZone=timeZone_;
  if(device_.percent)st.battery=*device_.percent;
  switch(device_.charge){case device::Charge::Charging:st.charge="Charging";break;case device::Charge::Full:st.charge="Full";break;case device::Charge::Discharging:st.charge="On battery";break;case device::Charge::NotCharging:st.charge="Not charging";break;case device::Charge::Unknown:break;}
  if(device_.percent||device_.charge!=device::Charge::Unknown)st.externalPower=device_.external?1:0;
  if(wifiLink_)st.wifiLink=*wifiLink_?1:0;
  st.storageTotal=storage_.total;st.storageFree=storage_.available;st.documents=int(std::count_if(all_.begin(),all_.end(),[](const auto& d){return !d.trashed;}));st.notes=int(notes_.size());st.firmware=firmware_;
  st.about={{"Typefaces","Caveat, Source Serif 4, Source Sans 3"},{"Font license","SIL Open Font License"},{"Dictionary","WordNet 3.0 and GCIDE \xC2\xB7 offline"},{"Dictionary source","Princeton University; GNU GCIDE (Webster 1913)"}};
  S::paint(c,targets_,st);}
 ib::library::overlays(c,targets_,m);
 if(keyboard_){ib::KeyboardModel k;k.title="Search titles";k.value=utf8(query_);k.hint="Type a title";k.shift=shift_;k.symbols=symbols_;ib::keyboard(c,targets_,k);}
}
LibraryView::Hit LibraryView::hit(int x,int y){
 if(x<0||x>=1620||y<0||y>=2160)return {};
 auto changed=[&](){rebuild();return Hit{Action::Changed,{},-1};};
 int id=targets_.find(x,y);namespace L=ib::library;
 if(create_){create_=false;switch(id){case MenuNotebook:return {Action::NewNotebook,{},-1};case MenuImport:return {Action::Import,{},-1};default:return changed();}}
 if(menu_){menu_=false;if(id==MenuLeave)return {Action::Exit,{},-1};if(id==MenuWifi)return {Action::Wifi,{},-1};if(id<L::NavBase||id>=L::NavBase+int(Section::Count))return changed();} /* the drawer: a section row falls through to navigation */
 if(keyboard_){
  if(id>=ib::KeyBase&&id<ib::KeyBase+128){if(query_.size()<48)query_+=QChar(char16_t(ib::keyCharacter(id,shift_)));shift_=false;offset_=0;return changed();}
  switch(id){case ib::KeyBackspace:query_.chop(1);offset_=0;return changed();case ib::KeySpace:if(query_.size()<48)query_+=' ';return changed();case ib::KeyShift:shift_=!shift_;return changed();
   case ib::KeySymbols:symbols_=!symbols_;return changed();case ib::KeyDone:keyboard_=false;return changed();case ib::KeyClear:query_.clear();offset_=0;return changed();case ib::KeyCancel:case L::ClearSearch:query_.clear();keyboard_=false;offset_=0;return changed();default:break;}
  if(y>=ib::KeyboardRect.y0)return {};
 }
 if(id>=L::NavBase&&id<L::NavBase+int(Section::Count)){auto s=Section(id-L::NavBase);if(s==Section::Shared)return {Action::Shared,{},-1};if(s==Section::Tom)return {Action::Tom,{},-1};
  section_=s;folder_.clear();query_.clear();keyboard_=false;offset_=0;notice_.clear();rebuild();return {s==Section::Notes?Action::NotesChanged:Action::Changed,{},-1};}
 if(id>=L::FolderBase&&id<L::FolderBase+L::folderCapacity()){int i=id-L::FolderBase;if(i<folders_.size()){folder_=folders_[i];offset_=0;return changed();}return {};}
 if(id>=L::DocumentBase&&id<L::DocumentBase+capacity()){int n=offset_+id-L::DocumentBase;if(n<filtered_.size()){int i=filtered_[n];return {Action::Open,all_[i].id,i};}return {};}
 if(section_==Section::Notes&&id>=HubOffset){int k=id-HubOffset;
  if(k>=ib::notes::FilterBase&&k<ib::notes::FilterBase+5){noteFilter_=ib::notes::Filter(unsigned(k-ib::notes::FilterBase));noteOffset_=0;return changed();}
  if(k>=ib::notes::StarBase&&k<ib::notes::StarBase+ib::notes::HubPageSize){int i=k-ib::notes::StarBase;if(i<visibleNotes_.size())return {Action::NoteFavorite,notes_[visibleNotes_[i]].document,-1,visibleNotes_[i]};return {};}
  if(k>=ib::notes::EntryBase&&k<ib::notes::EntryBase+ib::notes::HubPageSize){int i=k-ib::notes::EntryBase;if(i<visibleNotes_.size())return {Action::OpenNote,notes_[visibleNotes_[i]].document,-1,visibleNotes_[i]};return {};}
  if(k==ib::notes::HubPrev){noteOffset_=std::max(0,noteOffset_-ib::notes::HubPageSize);return changed();}
  if(k==ib::notes::HubNext){if(noteOffset_+ib::notes::HubPageSize<filteredNotes_.size())noteOffset_+=ib::notes::HubPageSize;return changed();}
 }
 switch(id){
 case L::Wordmark:section_=Section::MyFiles;folder_.clear();query_.clear();offset_=0;return changed();
 case L::Search:keyboard_=true;shift_=false;symbols_=false;section_=section_==Section::Trash||section_==Section::Favorites?section_:Section::MyFiles;return changed();
 case L::ClearSearch:query_.clear();keyboard_=false;offset_=0;return changed();
 case L::Create:create_=true;return {Action::Changed,{},-1};
 case L::ViewMode:grid_=!grid_;offset_=0;return changed();
 case L::Menu:menu_=true;return {Action::Changed,{},-1};
 case L::Sort:recent_=!recent_;offset_=0;return changed();
 case L::Up:folder_.clear();offset_=0;return changed();
 case L::Prev:offset_=std::max(0,offset_-capacity());return changed();
 case L::Next:if(offset_+capacity()<filtered_.size())offset_+=capacity();return changed();
 case SettingEffects:return {Action::Effects,{},-1};
  case S::ReadingProgress:return {Action::ReadingProgress,{},-1};case S::SnapHighlights:return {Action::SnapHighlights,{},-1};
  case S::TomStyleBase:case S::TomStyleBase+1:return {Action::TomStyle,{},id-S::TomStyleBase};
  case S::TomHandBase:case S::TomHandBase+1:case S::TomHandBase+2:return {Action::TomHand,{},id-S::TomHandBase};
  case S::TomReveal:return {Action::TomReveal,{},-1};case S::HighContrast:return {Action::HighContrast,{},-1};case S::PlainText:return {Action::PlainText,{},-1};case S::ReduceMotion:return {Action::ReduceMotion,{},-1};
  case S::TimeZone:return {Action::TimeZone,{},-1};
  case S::PasscodeSet:return {Action::PasscodeSet,{},-1};case S::PasscodeOff:return {Action::PasscodeOff,{},-1};case S::CompanionManage:return {Action::Shared,{},-1};
  case SettingPageBase:case SettingPageBase+1:case SettingPageBase+2:case SettingPageBase+3:case SettingPageBase+4:case SettingPageBase+5:case SettingPageBase+6:case SettingPageBase+7:case SettingPageBase+8:
   settingsPage_=S::Page(unsigned(id-SettingPageBase));return changed();
 case SettingLeave:return {Action::Exit,{},-1};
 case SettingWifi:return {Action::Wifi,{},-1};
 case SettingSleepCover:return {Action::SleepCover,{},-1};
 case SettingToolHandle:return {Action::ToolHandle,{},-1};
 case SettingLeftHanded:return {Action::LeftHanded,{},-1};
 default:if(id>=SettingSleepIdleBase&&id<SettingSleepIdleBase+int(SleepIdleChoices.size()))return {Action::SleepIdle,{},SleepIdleChoices[std::size_t(id-SettingSleepIdleBase)]};return {};}
}
bool LibraryView::page(int delta){if(delta==0||keyboard_||menu_||create_)return false;
 if(section_==Section::Notes){const int before=noteOffset_;if(delta<0)noteOffset_=std::max(0,noteOffset_-ib::notes::HubPageSize);else if(noteOffset_+ib::notes::HubPageSize<filteredNotes_.size())noteOffset_+=ib::notes::HubPageSize;if(noteOffset_==before)return false;rebuild();return true;}
 const int before=offset_;if(delta<0)offset_=std::max(0,offset_-capacity());else if(offset_+capacity()<filtered_.size())offset_+=capacity();if(offset_==before)return false;rebuild();return true;}
void LibraryView::showSection(Section s){section_=s;folder_.clear();query_.clear();keyboard_=false;menu_=false;create_=false;offset_=0;rebuild();}
}
