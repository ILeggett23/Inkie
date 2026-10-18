#include <new>
#include "ux_shell.h"
#include "panel_damage.h"
#include "e2_provisional_v2.h"
#include "product_bridge.h"
#include "release_policy.h"
#include "helper_disposition.h"
#include "tom_source.h"
#include "semantic_range.h"
#include "notes_runtime.h"
#include "product_services.h"
#include "document_view_store.h"
#include "epub_view_store.h"
#include "epub_stage_helper.h"
#include "ib/sheets.h"
#include "annotation_geometry.h"
#include <sys/resource.h>
#include <ctime>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QRegularExpression>
namespace ux {
namespace {
// Microseconds since lap, then restart the lap (device timing marks for the open-ready tick).
unsigned long long lapUs(std::chrono::steady_clock::time_point& lap)noexcept{auto now=std::chrono::steady_clock::now();auto us=std::chrono::duration_cast<std::chrono::microseconds>(now-lap).count();lap=now;return us>0?static_cast<unsigned long long>(us):0ULL;}
bool epubInkPins(const QJsonObject& doc){if(!doc["strokes"].isArray()||!doc["strokes"].toArray().isEmpty())return true;for(const auto* key:{"undo","redo"}){if(!doc[key].isArray())return true;for(auto v:doc[key].toArray()){auto history=v.toObject();if(history.contains("delta")){if(history["delta"].toObject().contains("strokes"))return true;continue;}for(const auto* side:{"before","after"}){auto state=history[side].toObject();if(!state["strokes"].isArray()||!state["strokes"].toArray().isEmpty())return true;}}}return false;}}
bool catalogueValid(const std::filesystem::path& app)noexcept{try{return app.is_absolute()&&underlay::digest(underlay::readBounded(app/"fixtures/inkbound-device-test.pdf",1024*1024)).toHex()==underlay::FixtureHash;}catch(...){return false;}}
Shell::Shell(const std::filesystem::path& app,const std::filesystem::path& data,bool libraryMode,bool companionEnabled,bool tomNetworkEnabled,reader::lifecycle::Tracking canonicalTracking,reader::lifecycle::Tracking notesTracking,reader::lifecycle::Tracking tomTracking,reader::lifecycle::Tracking preparationTracking,reader::lifecycle::Tracking resumeTracking):light_(preparationTracking),covers_(preparationTracking),canonicalTracking_(canonicalTracking),notesTracking_(notesTracking),tomTracking_(tomTracking),preparationTracking_(preparationTracking),resumeTracking_(resumeTracking),app_(app),data_(data),selectedData_(data),libraryMode_(libraryMode),companionEnabled_(companionEnabled&&release::policy(app).networking),tomNetworkEnabled_(tomNetworkEnabled&&release::policy(app).networking),engineScratch_(libraryMode?underlay::Pixels:0,0xffffffffu),toolsPixels_(libraryMode?underlay::Pixels:0,0xffffffffu),viewPixels_(libraryMode?underlay::Pixels:0,0xffffffffu),clean_(underlay::Pixels,0xffffffffu),chrome_(underlay::Pixels,0xffffffffu),semanticOverlay_(underlay::Pixels,0),semantics_(std::make_unique<semantic::Controller>(app,data)){ib::text::load(app_/"assets/fonts");if(libraryMode_)try{auto ids=QJsonDocument::fromJson(underlay::readBounded(data_/"ui-favorites.json",256*1024)).object()["ids"].toArray();for(auto v:ids)if(library::validId(v.toString()))favorites_.insert(v.toString());}catch(...){}if(libraryMode_)noteFavorites_.load(data_/"ui-note-favorites.json");if(libraryMode_){timeSetting_=clockprefs::load(data_);statusBarPixels_.reserve(500*112);pollClock(QDateTime::currentSecsSinceEpoch());}if(libraryMode_)bookmarks_.load(data_/"ui-bookmarks.json");if(!preparationTracking_.supports(recovery::v2::WorkerKind::CancelableHelperPrefix)||!resumeTracking_.supports(recovery::v2::WorkerKind::DurablePrefix)||!canonicalTracking_.supports(recovery::v2::WorkerKind::DurablePrefix)||!notesTracking_.supports(recovery::v2::WorkerKind::DurablePrefix)||!tomTracking_.supports(recovery::v2::WorkerKind::DurablePrefix))throw std::runtime_error("shell-progress-kind");if(libraryMode_){prefs_=Prefs::load(data_);applyPrefs();reloadLibrary();libraryPaint();if(companionEnabled_)startCompanion("status");if(passcodeSet())beginLock(Lock::Unlock);}else picker({chrome_,underlay::Width,underlay::Height,underlay::Stride});
 releaseNetworking_=release::policy(app_).networking; /* read once: the policy is a startup boundary (release_policy.h) */
 timespec boot{},mono{};clock_gettime(CLOCK_BOOTTIME,&boot);clock_gettime(CLOCK_MONOTONIC,&mono);suspendedNs_=(std::int64_t(boot.tv_sec)-mono.tv_sec)*1000000000LL+(boot.tv_nsec-mono.tv_nsec);}
Shell::~Shell(){if(!finish())std::_Exit(78);}
bool Shell::choose(unsigned index){if(timezoneView_.active()||stop_)return false;covers_.cancel();quickSettings_=quickSettingsWait_=false;recovery::v2::e2::mark("reader:open-tap",index);if((libraryMode_?(index>=unsigned(books_.size())||books_[index].trashed):index!=0)||state_!=State::Picker||opener_.joinable()||(!libraryMode_&&!catalogueValid(app_))){++rejected_;return false;}
 if(libraryMode_){try{epubZoom_.reset();selectedId_=books_[index].id;selectedTitle_=books_[index].title;selectedFormat_=books_[index].format;selectedData_=library::documentRoot(data_,selectedId_);openingLease_=std::make_unique<library::Lease>(selectedData_);if(!semantics_->finish(100)){openingLease_.reset();return false;}semantics_=std::make_unique<semantic::Controller>(app_,selectedData_);}catch(...){openingLease_.reset();++rejected_;setLibraryNotice("This book is unavailable \u2014 refresh the library");libraryPaint();change_.full=true;return false;}}
 if(!preparationTracking_.accept(openerTracking_)){openingLease_.reset();++rejected_;return false;}
 clearNavigation();done_=false;openFailed_=false;state_=State::Loading;if(libraryMode_){setLibraryNotice("Opening \u201c"+selectedTitle_+"\u201d\u2026");libraryPaint();damage(ib::library::NoticeRect);}else{pickerRow({chrome_,underlay::Width,underlay::Height,underlay::Stride},true);change_.regions|=8;}
 startOpener([this]{try{if(setpriority(PRIO_PROCESS,static_cast<id_t>(syscall(SYS_gettid)),19))throw std::runtime_error("opener-priority");underlay::privateDirectory(selectedData_);auto descriptor=library::descriptor(selectedData_);int total=descriptor.isEmpty()?2:descriptor["pages"].toInt();openingViewSet_=descriptor["format"]=="pdf"?reader::viewstore::activeSet(selectedData_).id:QString{};if(descriptor["format"]=="epub"){auto active=reader::epubviews::activeSet(selectedData_);openingViewSet_=active.id;total=active.receipt["pages"].toInt();}auto saved=reader::readViewResume(selectedData_,total,openingViewSet_);openerReconciled_=false;auto next=reader::prepareViewPage(app_,selectedData_,saved.page,stop_,openingViewSet_);openerReconciled_=true;prepared_=std::make_unique<underlay::Prepared>(std::move(next));if(prepared_->metadata["pages"].toInt()!=total)throw std::runtime_error("catalogue-page-count");}catch(const std::exception& e){if(reader::lifecycle::reconciledHelperFailure(e))openerReconciled_=true;std::fprintf(stderr,"READER_OPEN_FAILED reason=%.*s\n",512,e.what());openFailed_=true;}catch(...){std::fputs("READER_OPEN_FAILED reason=unknown\n",stderr);openFailed_=true;}done_.store(true,std::memory_order_release);});return true;
}
void Shell::pageBackgrounds(){refreshStatus();loadingShown_=false;loadingChrome_=false;auto& p=document_->active().prepared;std::copy(p.background.begin(),p.background.end(),clean_.begin());
 // Preserve the qualified PageId/fit/ink geometry. The footer strip outside the
 // PDF's 2060-pixel content fit carries the quiet progress indicator.
 fill({clean_,underlay::Width,underlay::Height,underlay::Stride},{0,2060,1620,2160});semantic::highlights({clean_,underlay::Width,underlay::Height,underlay::Stride},semantics_->highlights());if(styled_){auto snapshot=styled_->snapshot()["documents"].toObject()[selectedId_].toObject();auto current=std::find_if(books_.begin(),books_.end(),[&](const auto& b){return b.id==selectedId_;});if(current!=books_.end()){auto page=product::pageSnapshot(*current,p);styled::Matrix matrix;auto values=p.metadata["pageToSurface"].toArray();for(int i=0;i<6;++i)matrix.value[i]=values[i].toDouble();noteIndicators_=notes::indicators(snapshot,page,matrix);notes::paintIndicators({clean_,1620,2160,1632},noteIndicators_);}}
 std::snprintf(counter_.data(),counter_.size(),"%d / %d",document_->page()+1,document_->total());paintChrome();
}
ib::reader::Chrome Shell::chromeModel()const{ib::reader::Chrome m;m.title=semantic::utf8(libraryMode_?selectedTitle_:QStringLiteral("Inkbound device test"));m.chapter=m.title;m.search=false;m.bookmark=libraryMode_&&document_;m.bookmarked=m.bookmark&&bookmarks_.has(selectedId_,bookmarkLayout(),page());m.loading=loadingChrome_;
 if(document_){m.page=document_->page();m.total=document_->total();}
 QString status=notice_;
 if(status.isEmpty()&&styled_&&styled_->status().label){std::string_view l=styled_->status().label;
  if(l=="ACTIVE LAYER LOCKED OR HIDDEN")status="This layer is hidden or locked — open Layers to write";
  else if(l=="INTERRUPTED STROKE RECOVERED")status="An interrupted stroke was recovered — check its ending";
  else if(l=="INTERRUPTED ERASER RETAINED - ORIGINAL INK UNCHANGED")status="An interrupted erase was not applied — your ink is unchanged";
  else if(l=="UNRESOLVED TEXT ANCHORS - OPEN NOTES")status.clear();}
 m.status=semantic::utf8(status);return m;}
void Shell::saveToolShortcuts(){try{QJsonArray presets;for(const auto& style:toolShortcuts_)presets.append(styled::jsonStyle(style));underlay::atomicFile(data_/"ui-tool-shortcuts.json",QJsonDocument(QJsonObject{{"version",1},{"active",int(activeShortcut_)},{"slots",presets}}).toJson(QJsonDocument::Compact));shortcutsStored_=true;}catch(...){notice_="Tool shortcuts could not be saved";}}
void Shell::rememberShortcut(){if(!styled_||!styled_->status().ready||styled_->eraser())return;toolShortcuts_[activeShortcut_]=styled_->style();saveToolShortcuts();}
void Shell::syncToolShortcuts(){if(!styled_||!styled_->status().ready||styled_->status().contact)return;
 if(!shortcutsLoaded_){shortcutsLoaded_=true;auto current=styled_->style();activeShortcut_=current.tool==styled::Tool::Highlighter?1u:0u;toolShortcuts_[activeShortcut_]=current;
  try{const auto o=QJsonDocument::fromJson(underlay::readBounded(data_/"ui-tool-shortcuts.json",4096)).object();auto presets=o["slots"].toArray();product::require(o["version"]==1&&presets.size()==2&&o["active"].isDouble()&&o["active"].toDouble()==o["active"].toInt(-1)&&o["active"].toInt(-1)>=0&&o["active"].toInt()<=1,"tool-shortcut-schema");auto first=styled::parseStyle(presets[0].toObject()),second=styled::parseStyle(presets[1].toObject());toolShortcuts_={first,second};activeShortcut_=unsigned(o["active"].toInt());shortcutsStored_=true;}catch(...){/* Legacy installs retain their current writing style until a shortcut is chosen. */}
 }
 if(shortcutApplyPending_){if(!shortcutsStored_||styled_->selectStyle(toolShortcuts_[activeShortcut_]))shortcutApplyPending_=false;}
}
ib::reader::Tools Shell::railModel()const{ib::reader::Tools t;if(!styled_)return t;auto style=styled_->style();auto status=styled_->status();
 t.highlighter=style.tool==styled::Tool::Highlighter;t.pen=ib::Instrument(unsigned(style.tool));t.primary=ib::Instrument(unsigned(activeShortcut_==0?style.tool:toolShortcuts_[0].tool));t.secondary=ib::Instrument(unsigned(activeShortcut_==1?style.tool:toolShortcuts_[1].tool));t.primaryColor=activeShortcut_==0?style.color:toolShortcuts_[0].color;t.secondaryColor=activeShortcut_==1?style.color:toolShortcuts_[1].color;t.activeShortcut=activeShortcut_;t.color=style.color;t.ink=ib::theme::Ink[t.color%8];
 t.width=ib::reader::Width(unsigned(style.width));t.eraser=styled_->eraser();t.selecting=selectMode_;t.highlightColor=styled_->highlighterColor();t.busy=status.busy||!status.ready;t.panel=toolsView_.active();t.menu=menu_&&!(menuAnchor_.x0==RecentsAnchor.x0&&menuAnchor_.y0==RecentsAnchor.y0)&&!quickBrowsing();
 auto doc=styled_->snapshot()["documents"].toObject()[selectedId_].toObject();t.canUndo=!doc["undo"].toArray().isEmpty();t.canRedo=!doc["redo"].toArray().isEmpty();t.quietEffects=styled_->revealEnabled();return t;}
void Shell::paintChrome(bool force){toolHandleShown();auto m=chromeModel();const bool handle=styled_&&toolHandleShown();m.menuMark=!handle; /* without the handle the footer is the way into the toolbar */
 paintedTools_=railModel();paintedStyledStatus_=m.status;toolsPainted_=true;
 const double zoom=pdfZoom();
 {ib::Canvas c({clean_,1620,2160,1632});cleanTargets_.clear();m.progress=readingProgress();ib::reader::footer(c,cleanTargets_,m);ib::reader::cleanZones(cleanTargets_);if(handle)ib::reader::toolHandle(c,cleanTargets_,railModel());if(zoom>1.001)ib::reader::zoomBadge(c,cleanTargets_,int(std::lround(zoom*100)));}
 chromeStale_=!force&&state_==State::Clean&&!menu_;if(chromeStale_)return;
 std::copy(clean_.begin(),clean_.end(),chrome_.begin());
 ib::Canvas c({chrome_,1620,2160,1632});chromeTargets_.clear();chromeTargets_.add(ib::reader::Footer,ib::reader::FooterRect);ib::reader::closeButton(c,chromeTargets_);if(zoom>1.001)chromeTargets_.add(ib::reader::ZoomBadge,ib::reader::ZoomBadgeRect);
 if(styled_)ib::reader::toolbar(c,chromeTargets_,railModel());
 if(menu_)paintMenu();}
bool Shell::toolHandleShown()noexcept{if(!toolHandleLoaded_){toolHandleLoaded_=true;try{auto o=QJsonDocument::fromJson(underlay::readBounded(data_/"ui-toolbar.json",4096)).object();if(o["version"]==1&&o["handle"].isBool())toolHandle_=o["handle"].toBool();if(o["version"]==1&&o["side"].isString())leftHanded_=o["side"].toString()=="right";if(o["version"]==1&&o["progress"].isBool())readingProgress_=o["progress"].toBool();if(o["version"]==1&&o["snap"].isBool())snapHighlights_=o["snap"].toBool();}catch(...){}
  ib::reader::setToolbarSide(leftHanded_?ib::reader::Side::Right:ib::reader::Side::Left);}return toolHandle_;}
bool Shell::leftHanded()noexcept{toolHandleShown();return leftHanded_;}
void Shell::saveToolbarPrefs(){try{underlay::atomicFile(data_/"ui-toolbar.json",QJsonDocument(QJsonObject{{"version",1},{"handle",toolHandle_},{"side",leftHanded_?"right":"left"},{"progress",readingProgress_},{"snap",snapHighlights_}}).toJson(QJsonDocument::Compact));}catch(...){setLibraryNotice("Toolbar setting not saved");}}
void Shell::setToolHandle(bool shown){toolHandleShown();toolHandle_=shown;saveToolbarPrefs();}
void Shell::setReadingProgress(bool shown){toolHandleShown();readingProgress_=shown;saveToolbarPrefs();}
void Shell::setSnapHighlights(bool on){toolHandleShown();snapHighlights_=on;saveToolbarPrefs();}
void Shell::setLeftHanded(bool left){toolHandleShown();leftHanded_=left;ib::reader::setToolbarSide(left?ib::reader::Side::Right:ib::reader::Side::Left);saveToolbarPrefs();}
void Shell::paintMenu(){menuTargets_.clear();if(!document_){menu_=false;return;}ib::Canvas c({chrome_,1620,2160,1632});
 if(menuAnchor_.x0==LightAnchor.x0){auto state=light_.view();std::vector<ib::sheets::MenuItem> items;if(state.available&&!state.busy){items.push_back({LightOff,ib::Icon::Close,"Off"});items.push_back({LightLess,ib::Icon::ChevronDown,"Lower"});items.push_back({LightMore,ib::Icon::ChevronUp,"Higher"});}items.push_back({LightBack,ib::Icon::Back,"Back"});const auto label=state.busy?QString("Reading light — updating…"):!state.error.isEmpty()?state.error:state.available?QString("Reading light — %1 of 5").arg(state.power==4?0:ib::status::nearestStep(state.requested,state.maximum)):QString("Reading light");menuRect_=ib::sheets::documentMenu(c,menuTargets_,ib::reader::toolbarSlot(ib::reader::SlotDocument),semantic::utf8(label),items,items.size());return;}
 if(quickBrowsing()){ib::sheets::QuickBrowse m;m.title=semantic::utf8(selectedTitle_);m.page=page();m.total=document_->total();m.start=quickBrowseStart_;
  m.position=std::to_string(std::clamp((page()+1)*100/std::max(1,m.total),0,100))+"%";menuRect_=ib::sheets::quickBrowse(c,menuTargets_,m);return;}
 if(menuAnchor_.x0==RecentsAnchor.x0&&menuAnchor_.y0==RecentsAnchor.y0){QVector<library::Document> recent;for(const auto& b:books_)if(!b.trashed&&b.opened>0)recent.push_back(b);
  std::sort(recent.begin(),recent.end(),[](const auto& a,const auto& b){return a.opened>b.opened;});std::vector<ib::sheets::RecentBook> view;recentIds_.clear();
  for(const auto& b:recent){if(view.size()==5)break;recentIds_.push_back(b.id);view.push_back({semantic::utf8(b.title),semantic::utf8(b.format.toUpper()),semantic::utf8(b.format),b.id==selectedId_});}
  menuRect_=ib::sheets::recents(c,menuTargets_,view);return;}
 // Native document menu, attached to the toolbar's document tile. Layers is a toolbar tile, so it is not repeated here.
 const bool pdf=selectedFormat_=="pdf";std::vector<ib::sheets::MenuItem> items{{MenuSettings,ib::Icon::BookOpen,pdf?"View and zoom":"Text and layout"},{MenuPages,ib::Icon::Pages,"Page overview"}};
 if(libraryMode_){bool marked=bookmarks_.has(selectedId_,bookmarkLayout(),page());items.push_back({MenuBookmark,marked?ib::Icon::BookmarkFilled:ib::Icon::Bookmark,marked?"Remove bookmark":"Bookmark this page"});}
 std::size_t separator=items.size();
 if(styled_){items.push_back({MenuNotes,ib::Icon::Notes,"Document notes"});items.push_back({MenuTom,ib::Icon::Tom,"Ask Tom about this page"});}
 items.push_back({MenuAbout,ib::Icon::Info,pdf?"About this document":"About this book"});
 menuRect_=ib::sheets::documentMenu(c,menuTargets_,ib::reader::toolbarSlot(ib::reader::SlotDocument),semantic::utf8(selectedTitle_.isEmpty()?QStringLiteral("Document"):selectedTitle_),items,separator);}
// Quick browse (swipe up from the bottom edge): stays open while pages change underneath, until a tap outside it.
bool Shell::openQuickBrowse(){if(stop_||!document_||document_->contact()||(tom_&&tom_->active())||(notes_&&notes_->active())||viewControls_.active()||(state_!=State::Clean&&state_!=State::Chrome))return false;
 toolsView_={};if(!quickBrowsing())quickBrowseStart_=page();menuAnchor_=QuickBrowseAnchor;menu_=true;state_=State::Chrome;paintChrome(true);change_.regions|=ChromeRegions;damage(menuRect_);return true;}
bool Shell::quickBrowseTap(int id,int x){using namespace ib::sheets;const int total=document_->total(),current=page();int target=current;
 if(id==QuickTrack){const float f=float(std::clamp(x,QuickTrackRect.x0,QuickTrackRect.x1)-QuickTrackRect.x0)/float(QuickTrackRect.x1-QuickTrackRect.x0);target=int(std::lround(f*float(std::max(0,total-1))));}
 else if(id==QuickReturn)target=quickBrowseStart_;else if(id==QuickPrevious)target=current-1;else if(id==QuickNext)target=current+1;
 target=std::clamp(target,0,std::max(0,total-1));if(target==current)return true;
 if(!navigateTo(target)){notice("The page is still saving \u2014 try again in a moment");return true;}return true;}
bool Shell::openRecents(){if(stop_||!libraryMode_)return false;
 if(!document_){if(state_!=State::Picker)return false;libraryView_.showSection(LibraryView::Section::Recents);libraryRefresh();return true;}
 if(document_->contact()||(tom_&&tom_->active())||(notes_&&notes_->active())||viewControls_.active()||(state_!=State::Clean&&state_!=State::Chrome))return false;
 toolsView_={};menuAnchor_=RecentsAnchor;menu_=true;state_=State::Chrome;paintChrome();change_.regions|=ChromeRegions;damage(menuRect_);return true;}
bool Shell::libraryPage(int delta){if(stop_||!libraryMode_||document_||state_!=State::Picker||wifiView_.active()||libraryManage_.active()||companionView_.active()||notebook_.active())return false;
 if(!libraryView_.page(delta))return false;libraryRefresh();return true;}
bool Shell::menuAction(int id){if(id>=MenuLight&&id<=LightBack){const auto previous=menuRect_;if(id==LightBack)menuAnchor_=ib::reader::toolbarSlot(ib::reader::SlotDocument);else{menuAnchor_=LightAnchor;if(id==MenuLight)(void)light_.request(app_);else{auto state=light_.view();if(state.available&&!state.busy){int current=state.power==4?0:ib::status::nearestStep(state.requested,state.maximum);int level=id==LightOff?0:ib::status::nativeLevel(std::clamp(current+(id==LightMore?1:-1),0,5),state.maximum);if(!light_.request(app_,level))notice_="Reading light is busy";}}}menu_=true;paintChrome(true);damage(previous);damage(menuRect_);return true;}menu_=false;pageBackgrounds();damage(menuRect_);
 if(id>=ib::sheets::RecentBase&&id<ib::sheets::RecentBase+5){int n=id-ib::sheets::RecentBase;if(n>=recentIds_.size()||recentIds_[n]==selectedId_)return true;pendingOpen_=recentIds_[n];if(returnToLibrary())return true;pendingOpen_.clear();return false;}
 if(id==ib::sheets::RecentsLibrary){if(!returnToLibrary())return false;libraryView_.showSection(LibraryView::Section::Recents);return true;}
 damage(ib::reader::toolbarSlot(ib::reader::SlotDocument)); /* the document tile loses its black selection */
 switch(id){case MenuNotes:return openNotes(notes::Mode::Hub);case MenuTom:return openTom();case MenuLayers:return openNotes(notes::Mode::Layers);
  case MenuSettings:return openSettings(selectedFormat_=="pdf"?0:1);case MenuPages:return openSettings(2);case MenuAbout:return openSettings(3);case MenuLibrary:return returnToLibrary();
  case MenuBookmark:if(!document_||!bookmarks_.toggle(selectedId_,bookmarkLayout(),page(),document_->total(),document_->active().prepared.metadata["pdf"].toString(),currentViewSet_))notice("The bookmark could not be saved");return true;
  default:return true;}}
bool Shell::railAction(int id){if(!styled_||!document_||document_->contact())return false;
 /* The chrome as it is on screen before this tap: a rail action may repaint the chrome on its way (semanticPaint,
    selection, eraser), so toolsPaint() must diff against what was shown, not against its own entry. */
 if(state_==State::Chrome&&!chromeStale_){libraryBefore_.assign(chrome_.begin(),chrome_.end());chromeBeforeHeld_=true;}
 struct Release{bool& held;~Release(){held=false;}}release{chromeBeforeHeld_}; // paths that open a surface never diff
 syncToolShortcuts();using namespace ib::reader;bool ok=true;
 auto chooseShortcut=[&](unsigned slot){const bool selecting=selectMode_,erasing=styled_->eraser();selectMode_=false;
  if(activeShortcut_==slot&&!selecting&&!erasing){rememberShortcut();if(toolsView_.active())toolsView_={};else toolsView_.open(toolsSnapshot());return true;}
  if(!erasing)rememberShortcut();if(!styled_->selectStyle(toolShortcuts_[slot]))return false;activeShortcut_=slot;saveToolShortcuts();return true;};
 switch(id){
 case RailPen:ok=chooseShortcut(0);break;
 case RailHighlighter:ok=chooseShortcut(1);break;
 case RailEraser:selectMode_=false;ok=styled_->setEraser(!styled_->eraser());break;
 case RailSelect:{const bool selected=!selectMode_;if(selected&&!styled_->setEraser(false)){ok=false;break;}selectMode_=selected;selectionChrome_=selected&&state_==State::Chrome;toolsView_={};semantics_->dismiss();if(semantics_->takeDirty())semanticPaint();break;}
 case RailLayers:toolsView_={};return openNotes(notes::Mode::Layers);
 case RailUndo:ok=styled_->undo();if(ok)expectRemoval();break;
 case RailRedo:ok=styled_->redo();if(ok)expectRemoval();break;
 case RailTom:toolsView_={};return openTom({}); // no selection: a question about this page
 case RailPages:toolsView_={};return openSettings(2);
 case RailMore:toolsView_={};if(menu_){menu_=false;pageBackgrounds();damage(menuRect_);damage(toolbarSlot(SlotDocument));return true;}menuAnchor_=toolbarSlot(SlotDocument);menu_=true;paintChrome(true);damage(menuRect_);damage(toolbarSlot(SlotDocument));return true;
 default:return false;}
 if(!ok){recovery::v2::e2::mark("ui:tool-command-refused",unsigned(id));notice_="Still saving — try again in a moment";}
 ++toolsRevision_;if(toolsView_.active())toolsView_.update(toolsSnapshot());toolsPaint();return true;}
// The first failure names its call site in the device journal (reader:shell-fail value=<code>); later ones would only
// repeat the outcome. Codes are listed in docs/hardware-validation.md.
void Shell::fail(unsigned reason)noexcept{if(!failed_){recovery::v2::e2::mark("reader:shell-fail",reason);std::fprintf(stderr,"READER_SHELL_FAILURE reason=%u\n",reason);}failed_=true;state_=State::Failed;}
void Shell::notice(QString message){notice_=std::move(message);markNotice(notice_);
 if(libraryMode_&&!document_){setLibraryNotice(notice_);notice_.clear();if(librarySheetOpen())return;libraryPaint();damage(ib::library::NoticeRect);return;}
 if(document_){paintChrome();change_.regions|=4;}}
bool Shell::openSettings(int tab){if(!openViewControls())return false;viewControls_.showTab(DocumentControls::Tab(std::clamp(tab,0,3)));viewPaint();return true;}
void Shell::viewPaint(bool full){auto p=DocumentControls::Panel;auto& before=libraryBefore_; /* reused frame, never reallocated per tap */if(!full)before.assign(viewPixels_.begin(),viewPixels_.end());auto bg=background();for(int y=std::max(0,p.y0-4);y<std::min(2160,p.y1+4);++y)for(int x=std::max(0,p.x0-4);x<std::min(1620,p.x1+4);++x)viewPixels_[std::size_t(y)*1632+x]=bg[std::size_t(y)*1632+x];viewControls_.paint({viewPixels_,1620,2160,1632});if(full){tomDamage(ib::outset(p,4));return;}
 // Partial repaint (arriving thumbnails): refresh only the bands whose pixels changed, a row of pages at a time.
 std::array<spike::Rect,4> changed;const unsigned n=changedRegions(before,viewPixels_,1620,2160,1632,std::max(0,p.y0-4),changed);for(unsigned k=0;k<n;++k)damage(changed[k]);}
// Page overview thumbnails: asked for only while the Pages tab is showing and the reader is neither writing nor
// preparing a page; an arriving batch refreshes the sheet's region, never the whole screen.
void Shell::thumbnailTick(){
 const bool showing=libraryMode_&&document_&&!changingView_&&viewControls_.active()&&viewControls_.tab()==DocumentControls::Tab::Pages;
 if(thumbnails_.collect()&&showing)viewPaint(false);
 if(!document_){thumbnails_.reset();return;}
 if(!showing){thumbnails_.cancel();return;}
 if(document_->contact()||document_->preparing())return;
 (void)thumbnails_.request({app_,selectedData_,currentViewSet_,document_->total()},viewControls_.firstVisiblePage(),ib::sheets::OverviewPageSize);
}
DocumentControls::Snapshot Shell::viewSnapshot()const{DocumentControls::Snapshot s;if(!document_)return s;const auto& p=document_->active().prepared;s.binding={selectedId_.toStdString(),p.binding.toHex().toStdString(),p.metadata["layout"].toString().toStdString(),presentedVersion_};s.format=selectedFormat_=="pdf"?DocumentControls::Format::Pdf:DocumentControls::Format::Epub;s.page=page();s.pages=document_->total();if(libraryMode_)for(const auto& b:bookmarks_.places(selectedId_,bookmarkLayout(),s.pages))s.bookmarks.push_back({b.page,b.approximate});auto b=p.metadata["bounds"].toArray();s.sourceBounds={b[0].toDouble(),b[1].toDouble(),b[2].toDouble(),b[3].toDouble()};if(s.format==DocumentControls::Format::Pdf&&!currentViewSet_.isEmpty()){auto receipt=reader::viewstore::readSet(selectedData_,currentViewSet_);if(auto entry=reader::viewstore::pageEntry(receipt,page()))s.pdf=entry->view;}if(s.format==DocumentControls::Format::Epub&&!currentViewSet_.isEmpty()){auto receipt=reader::epubviews::readSet(selectedData_,currentViewSet_);auto layout=receipt.receipt["layout"].toObject();s.epub={layout["fontSize"].toInt(),layout["width"].toInt(),epub::parse(layout["userCss"].toString()).value_or(epub::Typography{})};}s.legacyInkPins=(styled_&&styled_->hasLegacyInk())||!semantics_->highlights().isEmpty();if(s.format==DocumentControls::Format::Epub&&styled_)s.layoutInk=epubInkPins(styled_->snapshot()["documents"].toObject()[selectedId_].toObject());s.busy=changingView_||(styled_&&styled_->status().busy)||semantics_->pending();s.contact=document_->contact();s.quietEffects=styled_&&styled_->revealEnabled();s.title=semantic::utf8(selectedTitle_);return s;}
bool Shell::openViewControls(){if(quickSettings_||wifiView_.active()||!document_||document_->contact()||(styled_&&styled_->status().busy))return false;toolsView_={};menu_=false;if(!viewControls_.open(viewSnapshot()))return false;viewControls_.setPageImages([this](int page)->ib::sheets::PageImage{if(auto i=thumbnails_.find(page))return {i->gray.data(),i->width,i->height};return {};});viewPaint();revealSceneChanged();return true;}
bool Shell::jumpBookmark(int index){if(!document_||document_->contact()||changingView_||opener_.joinable()||!styled_||styled_->status().busy||semantics_->pending())return false;auto places=bookmarks_.places(selectedId_,bookmarkLayout(),document_->total());if(index<0||index>=int(places.size()))return false;auto mark=bookmarks_.mark(selectedId_,places[std::size_t(index)].mark);if(!mark)return false;if(mark->source.isEmpty()){const int target=places[std::size_t(index)].page;viewControls_={};if(target!=page())return navigateTo(target);pageBackgrounds();tomDamage(ib::outset(DocumentControls::Panel,4));return true;}
 try{const auto current=std::find_if(books_.begin(),books_.end(),[&](const auto& b){return b.id==selectedId_;});if(current==books_.end()||mark->source!=current->sha256)throw std::runtime_error("bookmark-source-changed");if(selectedFormat_=="epub"){auto saved=reader::epubviews::readSet(selectedData_,mark->viewSet);if(saved.receipt["pages"].toInt()!=mark->pages||mark->page>=mark->pages)throw std::runtime_error("bookmark-layout-changed");}else{auto saved=reader::viewstore::readSet(selectedData_,mark->viewSet);if(saved.receipt["base_pages"].toInt()!=mark->pages||mark->page>=mark->pages)throw std::runtime_error("bookmark-page-changed");if(styled_->hasLegacyInk()||!semantics_->highlights().isEmpty())throw std::runtime_error("bookmark-view-pinned");}}
 catch(...){notice("The bookmarked view is unavailable — your book stays here");return false;}
 bookmarkView_=mark->viewSet;viewPage_=mark->page;restoringBookmark_=true;viewControls_={};toolsView_={};clearNavigation();changingView_=true;state_=State::NavigationPending;loadingChrome_=true;paintChrome();change_.full=true;return true;}
bool Shell::beginPdfPreview(const reader::pdfview::Plan& target){
 if(!document_||!styled_||styled_->status().busy||unsaved()||change_.full)return false;
 try{const auto a=document_->active().prepared.metadata["pageToSurface"].toArray();if(a.size()!=6)return false;std::array<double,6> old;for(int i=0;i<6;++i)old[i]=a[i].toDouble();styled::Matrix oldMatrix{old};if(!oldMatrix.valid())return false;const auto& n=target.inverse.value;
  pdfPreviewToOld_={old[0]*n[0]+old[2]*n[1],old[1]*n[0]+old[3]*n[1],old[0]*n[2]+old[2]*n[3],old[1]*n[2]+old[3]*n[3],old[0]*n[4]+old[2]*n[5]+old[4],old[1]*n[4]+old[3]*n[5]+old[5]};
  pdfPreviewPixels_.assign(underlay::Pixels,0xffffffffu);spike::Surface scratch{pdfPreviewPixels_,1620,2160,1632};styled_->composite(scratch,document_->active().prepared.background,{0,0,1620,2060});
  for(int y=2060;y<2160;++y)std::copy_n(clean_.data()+std::size_t(y)*1632,1620,pdfPreviewPixels_.data()+std::size_t(y)*1632);
  pdfPreviewActive_=true;return true;
 }catch(...){pdfPreviewActive_=false;return false;}
}
void Shell::composePdfPreview(spike::Surface out,spike::Rect r)const{
 auto part=ib::intersect(r,{0,0,1620,2060});const auto& m=pdfPreviewToOld_;
 for(int y=part.y0;y<part.y1;++y){double sx=m[0]*(part.x0+.5)+m[2]*(y+.5)+m[4],sy=m[1]*(part.x0+.5)+m[3]*(y+.5)+m[5];for(int x=part.x0;x<part.x1;++x,sx+=m[0],sy+=m[1]){const int px=int(std::floor(sx)),py=int(std::floor(sy));out.pixels[std::size_t(y)*out.stridePixels+x]=px>=0&&px<1620&&py>=0&&py<2060?pdfPreviewPixels_[std::size_t(py)*1632+px]:0xffffffffu;}}
 const auto footer=ib::intersect(r,{0,2060,1620,2160});for(int y=footer.y0;y<footer.y1;++y)std::copy_n(pdfPreviewPixels_.data()+std::size_t(y)*1632+footer.x0,footer.x1-footer.x0,out.pixels.data()+std::size_t(y)*out.stridePixels+footer.x0);
}
bool Shell::applyPdfView(reader::pdfview::View view,bool viewportOnly){if(stop_)return false;if(!libraryMode_||selectedFormat_!="pdf"||!document_||document_->contact()||changingView_||opener_.joinable()||!styled_||styled_->status().busy||styled_->hasLegacyInk()||semantics_->pending()||!semantics_->highlights().isEmpty()||(notes_&&notes_->active())||(tom_&&tom_->active()))return false;auto snapshot=viewSnapshot();auto plan=reader::pdfview::plan(snapshot.sourceBounds,view);styled::Matrix matrix{plan.forward.value};if(!matrix.valid())return false;const bool preview=viewportOnly&&beginPdfPreview(plan);requestedView_=view;viewBounds_=snapshot.sourceBounds;viewPage_=page();viewControls_={};clearNavigation();viewportRefresh_=viewportOnly;changingView_=true;state_=State::NavigationPending;loadingChrome_=true;paintChrome();if(viewportOnly){change_.regions|=ChromeRegions;if(preview){tomDamage({0,0,1620,2060});change_.preview=true;}}else change_.full=true;return true;}
bool Shell::applyEpubView(DocumentControls::EpubSettings view){if(stop_)return false;if(!libraryMode_||selectedFormat_!="epub"||!document_||document_->contact()||changingView_||opener_.joinable()||!styled_||styled_->status().busy||styled_->hasLegacyInk()||semantics_->pending()||!semantics_->highlights().isEmpty()||(notes_&&notes_->active())||(tom_&&tom_->active())||view.fontSize<6||view.fontSize>72||view.lineWidth<64||view.lineWidth>4094)return false;/* Handwriting no longer pins the layout: strokes are drawn only on the page and layout they were written on (styled
    matches pageId+layoutId), so in another layout they are hidden, unchanged, and the original layout shows them again. */
 requestedEpubView_=view;viewPage_=page();viewControls_={};clearNavigation();changingView_=true;state_=State::NavigationPending;loadingChrome_=true;paintChrome();change_.full=true;return true;}
void Shell::viewTick(){if(!document_||document_->contact())return;if(styled_){if(styled_->status().failed){fail(1);return;}if(!styled_->finish(10))return;}if(!semantics_->finish(10))return;if(semantics_->failed()){fail(2);return;}if(!document_->finish(10))return;if(semantics_->failed()||document_->failed()||unsaved()){fail(3);return;}retiredStyledCommits_+=styled_->status().saved;styled_.reset();styledPage_.clear();document_.reset();presentedInk_={};semantics_=std::make_unique<semantic::Controller>(app_,selectedData_);semanticVisual_={};std::fill(semanticOverlay_.begin(),semanticOverlay_.end(),0);if(!preparationTracking_.accept(openerTracking_)){fail(4);return;}if(!resumeTracking_.accept(openerResumeTracking_)){preparationTracking_.complete(openerTracking_,true);fail(5);return;}done_=false;openFailed_=false;changingView_=false;state_=State::Loading;auto old=currentViewSet_;auto view=requestedView_;auto bounds=viewBounds_;int target=viewPage_;const bool restore=restoringBookmark_;const auto bookmark=bookmarkView_;restoringBookmark_=false;bookmarkView_.clear();
 startOpener([this,old,view,bounds,target,restore,bookmark]{try{int destination=target;QString next;bool refused=false;if(restore){if(selectedFormat_=="epub"){auto saved=reader::epubviews::readSet(selectedData_,bookmark);if(target<0||target>=saved.receipt["pages"].toInt())throw std::runtime_error("bookmark-page-invalid");next=bookmark;}else{auto saved=reader::viewstore::readSet(selectedData_,bookmark);auto active=reader::viewstore::readSet(selectedData_,old);auto entry=reader::viewstore::pageEntry(saved,target);auto existing=reader::viewstore::pageEntry(active,target);next=entry?reader::viewstore::stagePdf(selectedData_,old,target,entry->bounds,entry->view):existing?reader::viewstore::stagePdf(selectedData_,old,target,existing->bounds,{}):old;}}else if(selectedFormat_=="epub"){openerReconciled_=false;
   // A layout the stage helper refuses (e.g. this reading position can't be placed in it) keeps the book open in its
   // current layout on the same page, with a notice: a refused text size is not a failed open.
   try{auto staged=reader::stageEpubViewViaHelper(app_,selectedData_,old,requestedEpubView_.lineWidth,requestedEpubView_.fontSize,target,stop_,requestedEpubView_.typography);next=staged.viewId;destination=staged.targetPage;}
   catch(const std::exception& e){if(stop_||!reader::lifecycle::reconciledHelperFailure(e))throw;next=old;destination=target;refused=true;recovery::v2::e2::mark("ui:layout-refused");}
   openerReconciled_=true;}else next=reader::viewstore::stagePdf(selectedData_,old,target,bounds,view);openerReconciled_=false;auto p=reader::prepareViewPage(app_,selectedData_,destination,stop_,next);openerReconciled_=true;auto descriptor=library::descriptor(selectedData_);int total=p.metadata["pages"].toInt();auto resume=reader::readViewResume(selectedData_,total,next);openerPositionKnown_=false;reader::writeViewResume(selectedData_,destination,total,resume.generation+1,false,next);if(refused){}else if(selectedFormat_=="epub")reader::epubviews::activate(selectedData_,next,old);else reader::viewstore::activate(selectedData_,next,old);viewRefused_=refused;openerPositionKnown_=true;openingViewSet_=next;prepared_=std::make_unique<underlay::Prepared>(std::move(p));}catch(const std::exception& e){if(reader::lifecycle::reconciledHelperFailure(e))openerReconciled_=true;prepared_.reset();openFailed_=true;}catch(...){prepared_.reset();openFailed_=true;}done_.store(true,std::memory_order_release);});}
// Closing Tom returns to the clean page, as closing Notes does; a toolbar left visible around the sheet is erased so no stale controls remain.
void Shell::tomClosed(){if(tomReturn_==State::Picker)change_.full=true;if(state_==State::Chrome){selectMode_=false;if(menu_){menu_=false;pageBackgrounds();damage(menuRect_);}change_.regions|=ChromeRegions;}state_=tomReturn_;}
bool Shell::openTom(semantic::Selection selection,QString note){if(stop_)return false;if(!libraryMode_||(document_&&document_->contact())||(styled_&&styled_->status().busy)||toolsView_.active()||(notes_&&notes_->active()))return false;if(!trimInactivePanel(true)){notice(QStringLiteral("Finishing the previous panel — try again in a moment"));return false;}QJsonObject page;try{if(document_){auto book=std::find_if(books_.begin(),books_.end(),[&](const auto& d){return d.id==selectedId_;});if(book==books_.end())return false;page=product::pageSnapshot(*book,document_->active().prepared);}if(!tom_)tom_=std::make_unique<tom::Panel>(app_,data_,tomNetworkEnabled_,tomTracking_,preparationTracking_);tomDestinationNote_=std::move(note);tomDestinationBinding_=tomDestinationNote_.isEmpty()?QJsonObject{}:page;tomReturn_=document_?State::Clean:State::Picker;tom_->setTitle(document_?selectedTitle_:QString());if(document_){backdrop(backdrop_);tom_->setBackdrop(backdrop_);}else{backdrop_.assign(underlay::Pixels,0xffffffffu);tom_->setBackdrop(backdrop_);}tomSelection_=selection;tom_->setPresentation(prefs_.tomTyped,prefs_.tomHand,prefs_.tomReveal&&!prefs_.reduceMotion);tom_->open(page,std::move(selection),true);}catch(const std::exception&){if(!tom_){notice("Tom could not open. Your reading session is safe.");return true;}if(!retireFailedTom()&&tom_&&tom_->unsaved()!=0)fail(6);return true;} /* Tom failed while opening (e.g. its store refused to open) */tomEpochStamp_=tom_->presentationStamp().ui;tomGestureStamp_=tom_->gestureEpoch();selectMode_=false;penSelect_={};penRangeCount_=0;penRangeOverflow_=false;chromeStale_=true; /* Tom opens ready to write: the selection tool is put away */semantics_->dismiss();semanticVisual_={};std::fill(semanticOverlay_.begin(),semanticOverlay_.end(),0);if(!document_)change_.full=true;else tomDamage(tom_->repaintArea());++presentedVersion_;revealSceneChanged();return true;}
// D2: a Tom failure that holds nothing unsaved ends Tom, not the reading session (only unsaved Tom input still fails it).
// The failed panel is stopped and dropped, the page or library comes back with a notice, and Tom can be opened again.
bool Shell::retireFailedTom(){if(!tom_)return true;const bool shown=tom_->active();{std::string reason=tom_->view().error.left(160).toStdString();for(auto& ch:reason)if(ch<32||ch>126)ch='?';std::fprintf(stderr,"TOM_PANEL_FAILED unsaved=%u reason=%s\n",tom_->unsaved(),reason.c_str());}
 if(tom_->unsaved()!=0||!tom_->retire())return false;recovery::v2::e2::mark("tom:retired");tom_.reset();
 /* A closed Tom whose worker failed later is simply dropped: restoring the state Tom was opened from would put the
    library over an open book (or a dead page over the library). */
 if(!shown)return true;tomClosed();if(document_)pageBackgrounds();else libraryPaint();change_.complete(FullRefreshReason::Recovery);++presentedVersion_;notice("Tom stopped \u2014 your page and notes are safe");return true;}
void Shell::tomTick(){if(!tom_||!tom_->active()||tom_->contact())return;tom_->tick();{auto r=tom_->takeReplyCleanup();if(!r.empty()){change_.cleanup.include(r);tomDamage(r);}}if(tom_->failed()){if(tom_->unsaved()==0){(void)retireFailedTom();return;}fail(6);return;}auto save=tom_->takeSaveRequest();if(!save.isEmpty()){if((!document_||!styled_)&&!save["ink"].toObject().isEmpty()&&tom_->canLeave()){tom_->closed();state_=State::Picker;pendingTomNote_={{"title",QString("Tom — ")+save["question"].toString().left(60)},{"ink",save["ink"]},{"waitNotebook",true}};libraryPaint();if(!openNewNotebook()){pendingTomNote_={};notice("Create or open a notebook to place Tom's ink");}change_.full=true;return;}else if(!document_||!styled_){tom_->sourceRejected();}else if(tom_->canLeave()){tom_->closed();tomClosed();pageBackgrounds();
  auto question=save["question"].toString().simplified(),answer=save["answer"].toString().trimmed();auto title=QString("Tom — ")+(question.isEmpty()?QString("a question about this page"):question.left(60));
  auto text=(question.isEmpty()?QString():QString("Q: ")+question+"\n\n")+"Tom: "+answer;auto ctx=save["context"].toObject();
  // Link to the passage Tom was asked about when it is still the open page.
  semantic::Selection link=tomSelection_&&tomSelection_->data["pageId"]==ctx["pageId"]?tomSelection_:semantic::Selection{};
  if(ctx["document"].toString()==selectedId_&&ctx["page"].toInt(-2)!=page()&&ctx["scope"]!="general"){pendingTomNote_={{"title",title},{"text",text},{"ink",save["ink"]},{"destination",selectedId_},{"source",ctx}};navigateTo(ctx["page"].toInt());}
  else if(openNotes(notes::Mode::Note,link,!save["ink"].toObject().isEmpty()?tomDestinationNote_:QString())){if(save["ink"].toObject().isEmpty())notes_->queueText(title,text);else notes_->queueInk(tomDestinationNote_.isEmpty()?title:QString(),save["ink"].toObject());}else notice("The note could not be created — Tom’s answer is kept in History");
  change_.full=true;++presentedVersion_;return;}else{tom_->showNotice("Wait until your question is saved");}}
auto native=tom_->takeNativeOutputRequest();if(!native.isEmpty()){auto target=std::find_if(books_.begin(),books_.end(),[&](const auto& d){return !d.trashed&&d.id==native.value("document").toString()&&d.sha256==native.value("revision").toString();});bool valid=target!=books_.end()&&library::validId(native.value("note").toString())&&library::validId(native.value("group").toString())&&library::validId(native.value("request").toString());if(valid&&tom_->canLeave()){pendingNativeOutput_=native;tom_->closed();tomClosed();if(document_&&selectedId_!=target->id){if(!returnToLibrary()){pendingNativeOutput_={};tom_->open({},{});tom_->showNotice("That saved reply is unavailable. Your history is kept.");}}else if(document_)pageBackgrounds();else libraryPaint();change_.full=true;++presentedVersion_;return;}tom_->showNotice("That saved reply is unavailable. Your history is kept.");}
auto source=tom_->takeSourceRequest();if(!source.isEmpty()){bool valid=false;auto id=source["document"].toString();auto target=tom::resolveSource(data_,source);for(const auto& d:books_)if(target&&d.id==id&&!d.trashed&&source["revision"]==d.sha256)valid=true;if(valid&&tom_->canLeave()){tom_->closed();tomSource_=source;tomClosed();if(document_&&id!=selectedId_){if(!returnToLibrary()){tomSource_={};tom_->open({},{});tom_->sourceRejected();}}change_.full=true;++presentedVersion_;return;}tom_->sourceRejected();}
 if(tom_->closeRequested()&&tom_->canLeave()){const auto oldArea=tom_->repaintArea();tom_->closed();tomClosed();if(document_){pageBackgrounds();if(!tomDestinationNote_.isEmpty()&&tomDestinationBinding_["document"]==selectedId_&&tomDestinationBinding_["page"].toInt(-1)==page())openNotes(notes::Mode::Note,{},tomDestinationNote_);}else libraryPaint();tomDamage(oldArea);change_.cleanup.include(oldArea);++presentedVersion_;return;}
 if(tom_->gestureEpoch()!=tomGestureStamp_){tomGestureStamp_=tom_->gestureEpoch();++presentedVersion_;}if(tom_->takeReset())tomDamage(tom_->repaintArea());std::array<spike::Rect,4> regions;auto count=tom_->takeDamage(regions);if(!change_.full)for(unsigned i=0;i<count;++i)tomDamage(regions[i]);}
void Shell::sourceTick(){
 if(!pendingOpen_.isEmpty()&&!returning_&&!document_&&state_==State::Picker){const auto id=pendingOpen_;pendingOpen_.clear();
  for(int i=0;i<books_.size();++i)if(books_[i].id==id&&!books_[i].trashed){choose(unsigned(i));return;}return;}
 if(!pendingNativeOutput_.isEmpty()&&!returning_&&!(tom_&&tom_->active())){const auto link=pendingNativeOutput_;auto book=std::find_if(books_.begin(),books_.end(),[&](const auto& d){return !d.trashed&&d.id==link.value("document").toString()&&d.sha256==link.value("revision").toString();});if(book==books_.end()){pendingNativeOutput_={};notice("That saved reply is unavailable. Your history is kept.");return;}if(!document_){if(state_==State::Picker&&!choose(unsigned(book-books_.begin()))){pendingNativeOutput_={};notice("That saved reply is unavailable. Your history is kept.");}return;}if(selectedId_!=book->id||!document_->writable()||awaitingSemanticPage_||!styled_||styled_->status().busy)return;pendingNativeOutput_={};if(!openNotes(notes::Mode::Note,{},link.value("note").toString(),{{"group",link.value("group")},{"request",link.value("request")}}))notice("That saved reply is unavailable. Your history is kept.");return;}
 if(tomSource_.isEmpty()||returning_||(tom_&&tom_->active()))return;
 const auto source=tomSource_;const auto id=source["document"].toString();const auto target=tom::resolveSource(data_,source);
 auto reject=[&]{tomSource_={};if(document_){semantics_->notify("The source changed \u2014 Tom\u2019s quoted excerpt is kept");semanticPaint();}else{setLibraryNotice("The source is unavailable \u2014 Tom\u2019s quoted excerpt is kept");libraryPaint();change_.full=true;}};
 if(!target){reject();return;}
 if(!document_){if(state_!=State::Picker)return;for(int i=0;i<books_.size();++i)if(books_[i].id==id&&!books_[i].trashed){if(!choose(unsigned(i)))reject();return;}reject();return;}
 if(selectedId_!=id||!document_->writable()||awaitingSemanticPage_)return;
 const auto& metadata=document_->active().prepared.metadata;
 if(document_->total()!=target->pages||(source["format"]=="epub"&&(currentViewSet_!=target->viewId||metadata.value("layout")!=target->layoutId))){reject();return;}
 if(document_->page()!=target->page){if(!navigateTo(target->page))reject();return;}
 auto book=std::find_if(books_.begin(),books_.end(),[&](const auto& d){return d.id==id&&!d.trashed;});
 // Tom freezes quoted text/context, not the drawable quads required by Notes.
 // Verify that exact quote against the actual destination before accepting it.
 const bool resolved=book!=books_.end()&&product::resolveAnchor(source,product::pageSnapshot(*book,document_->active().prepared))["status"]=="resolved";
 tomSource_={};if(!resolved)reject();
}
// Saved, closed panels retain several full-frame rasters. Drain the other
// panel before allocating a new one, so Notes and Tom do not accumulate both
// sets of buffers. Never destroy an active owner or any unsaved work.
bool Shell::trimInactivePanel(bool targetTom){
 if(notes_&&!notes_->active()&&(targetTom||notesRetiring_)){if(notes_->unsaved())return false;notesRetiring_=true;if(!notes_->retire(10))return false;notes_.reset();notesRetiring_=false;}
 if(tom_&&!tom_->active()&&(!targetTom||tomRetiring_)){if(tom_->unsaved())return false;tomRetiring_=true;if(!tom_->retire(10))return false;tom_.reset();tomRetiring_=false;}
 return true;
}
bool Shell::openNotes(notes::Mode mode,semantic::Selection selection,QString id,QJsonObject expectedNative){if(stop_)return false;if(!document_||!styled_||document_->contact()||styled_->status().busy||toolsView_.active())return false;auto book=std::find_if(books_.begin(),books_.end(),[&](const auto& d){return d.id==selectedId_;});if(book==books_.end())return false;if(!trimInactivePanel(false)){notice(QStringLiteral("Finishing the previous panel — try again in a moment"));return false;}if(!notes_)try{notes_=std::make_unique<notes::Panel>(product::featureDirectory(data_,*book),notesTracking_,app_);}catch(const std::bad_alloc&){notice(QStringLiteral("Notes could not open — your page and saved notes are safe"));return false;}if(libraryMode_)notes_->setFavorites(&noteFavorites_);notes_->setTitle(selectedTitle_);backdrop(backdrop_);notes_->setBackdrop(backdrop_);auto page=product::pageSnapshot(*book,document_->active().prepared);bool opened=mode==notes::Mode::Highlight?notes_->highlight(page,selection,styled_->highlighterColor(),true):notes_->open(mode,page,std::move(selection),styled_->style(),std::move(id),styled_->highlighterColor(),std::move(expectedNative));if(!opened)return false;selectMode_=false;penSelect_={};penRangeCount_=0;penRangeOverflow_=false;chromeStale_=true; /* Notes opens ready to write */semantics_->dismiss();semanticVisual_={};std::fill(semanticOverlay_.begin(),semanticOverlay_.end(),0);(void)notes_->takeReset();std::array<spike::Rect,5> initial;const auto count=notes_->takeDamage(initial);for(unsigned i=0;i<count;++i)tomDamage(initial[i]);++presentedVersion_;revealSceneChanged();return true;}
bool Shell::retireFailedNotes(){if(!notes_)return true;if(notes_->unsaved()!=0||!notes_->retire())return false;recovery::v2::e2::mark("notes:retired");notes_.reset();notesClosing_=false;notesReload_=true;state_=document_?State::Clean:State::Picker;if(document_)pageBackgrounds();else libraryPaint();change_.complete(FullRefreshReason::Recovery);++presentedVersion_;notice("Notes stopped \u2014 your saved notes are safe");return true;}
void Shell::notesTick(){if(!notes_||!notes_->active()||document_->contact())return;notes_->tick();if(notes_->failed()){if(notes_->unsaved()==0){(void)retireFailedNotes();return;}fail(7);return;}if(notes_->takeDocumentChanged())notesReload_=true;if(notes_->takeCanvasChanged())++presentedVersion_;const bool canvasContent=notes_->takeCanvasContentChanged();if(canvasContent)change_.content=true;auto tomRequest=notes_->takeTomRequest();if(!tomRequest.isEmpty()&&notes_->canLeave()){notes_->closed();state_=State::Clean;if(openTom({},tomRequest["note"].toString()))return;openNotes(notes::Mode::Note,{},tomRequest["note"].toString());}auto source=notes_->sourceRequest();int sourceTarget=notes_->sourceTarget();if(!source.isEmpty()&&notes_->canLeave()){auto book=std::find_if(books_.begin(),books_.end(),[&](const auto& d){return d.id==selectedId_;});bool ok=book!=books_.end()&&source["document"]==selectedId_&&source["revision"]==book->sha256;if(ok&&sourceTarget==document_->page())ok=notes::resolveAnnotation(source,product::pageSnapshot(*book,document_->active().prepared))["status"]=="resolved";else if(ok){ok=navigateTo(sourceTarget);if(ok)noteSource_=source;}notes_->sourceHandled(ok);}
 if(notes_->closeRequested()&&notes_->canLeave()){if(notesReload_&&!notesClosing_){if(!styled_->reload()){fail(8);return;}expectRemoval(); /* notes may have deleted highlights or hidden layers */notesReload_=false;notesClosing_=true;}if(notesClosing_){styled_->tick();if(styled_->status().failed){fail(9);return;}if(styled_->status().busy)return;notesClosing_=false;}const auto retired=notes_->repaintArea();notes_->closed();state_=noteSource_.isEmpty()?State::Clean:State::NavigationPending;pageBackgrounds();change_.regions|=ChromeRegions;tomDamage(retired);++presentedVersion_;return;}
 if(notes_->takeReset()){++presentedVersion_;}std::array<spike::Rect,5> regions;auto count=notes_->takeDamage(regions);if(!change_.full)for(unsigned i=0;i<count;++i){tomDamage(regions[i]);if(canvasContent){auto r=regions[i];if(change_.uiCleanupCount==change_.uiCleanup.size()){for(unsigned n=0;n<change_.uiCleanupCount;++n)r.include(change_.uiCleanup[n]);change_.uiCleanupCount=0;}change_.uiCleanup[change_.uiCleanupCount++]=r;}}}
bool Shell::bindStyled(){auto& prepared=document_->active().prepared;if(styled_&&styledPage_==prepared.binding){styled_->tick();return styled_->status().ready;}auto styledLap=std::chrono::steady_clock::now();if(styled_){if(!styled_->finish(10))return false;retiredStyledCommits_+=styled_->status().saved;styled_.reset();recovery::v2::e2::mark("reader:styled-finish-us",lapUs(styledLap));}library::Document selected;bool found=false;for(const auto& d:books_)if(d.id==selectedId_){selected=d;found=true;break;}if(!found)throw std::runtime_error("styled-document-missing");auto page=product::pageSnapshot(selected,prepared);styledPage_=prepared.binding;shortcutApplyPending_=true;std::fill(engineScratch_.begin(),engineScratch_.end(),0xffffffffu);recovery::v2::e2::mark("reader:styled-create",lapUs(styledLap));styled_=std::make_unique<styled::Ink>(product::featureDirectory(data_,selected),selectedData_/"features/legacy"/(styledPage_.toHex().toStdString()+".inkbase"),page,prepared.ink,underlay::Width,underlay::Height,underlay::Stride,page,canonicalTracking_);recovery::v2::e2::mark("reader:styled-construct-us",lapUs(styledLap));++toolsRevision_;return false;}
ToolsView::Snapshot Shell::toolsSnapshot()const{ToolsView::Snapshot s;s.binding={selectedId_.toStdString(),styledPage_.toHex().toStdString(),toolsRevision_};if(styled_){auto style=styled_->style();auto status=styled_->status();s.style={ToolsView::Tool(style.tool),ToolsView::Width(style.width),style.color,styled_->eraser()};s.busy=status.busy||!status.ready;s.contact=status.contact;auto doc=styled_->snapshot()["documents"].toObject()[selectedId_].toObject();s.canUndo=!doc["undo"].toArray().isEmpty();s.canRedo=!doc["redo"].toArray().isEmpty();s.quietEffects=styled_->revealEnabled();}s.snapHighlights=snapHighlights_;s.shortcut=activeShortcut_;return s;}
void Shell::toolsPaint(){
 /* A tool tap changes the chrome (a toolbar tile, the footer status), never the page: repaint the chrome only and,
    when it is already on screen, damage just the rects whose pixels changed instead of the whole chrome strip
    (top row, toolbar column and footer, about 0.5 Mpx of quality refresh for one 112 px tile). */
 /* A held frame (taken when a rail tap started) may be older than now: diffing against it can only damage more. */
 const bool shown=chromeBeforeHeld_||(state_==State::Chrome&&!chromeStale_);if(!chromeBeforeHeld_&&shown)libraryBefore_.assign(chrome_.begin(),chrome_.end());chromeBeforeHeld_=false;
 paintChrome(true);auto p=ToolsView::panel();if(toolsView_.active()){for(int y=p.y0;y<p.y1;++y)std::copy_n(chrome_.begin()+std::ptrdiff_t(std::size_t(y)*1632+p.x0),p.x1-p.x0,toolsPixels_.begin()+std::ptrdiff_t(std::size_t(y)*1632+p.x0));toolsView_.paint({toolsPixels_,1620,2160,1632});}
 if(state_==State::Clean)state_=State::Chrome;
 if(shown){std::array<spike::Rect,8> changed;const auto n=changedRegions(libraryBefore_,chrome_,1620,2160,1632,0,changed);for(unsigned i=0;i<n;++i)damage(changed[i]);}else change_.regions|=ChromeRegions;
 damage(p);revealSceneChanged();}
// UI area and queue pressure never imply a complete page transition. Large live
// regions are already banded by Display; retain all damage with bounded merging.
void Shell::damage(spike::Rect r){r=semantic::clip(r);if(r.empty()||change_.full)return;accumulateDamage(change_.extra,change_.extraCount,r);}
void Shell::tomDamage(spike::Rect r){r=semantic::clip(r);if(r.empty())return;/* Split the sheet into bounded regions before the existing damage cap; streaming does not request a full-page refresh. */const int height=std::max(1,int((1620ULL*2160/4)/std::uint64_t(r.x1-r.x0)));for(int y=r.y0;y<r.y1;y+=height)damage({r.x0,y,r.x1,std::min(r.y1,y+height)});}
Shell::UiScene Shell::uiScene()const{UiScene scene;if(panelShown()){scene.status=ib::status::Panel;scene.statusStyle=unsigned(std::max(0,ib::status::nearestStep(statusModel_.light,statusModel_.maximum)))+(statusModel_.busy?16u:0u);}scene.view=semantics_->view();scene.definitionOnly=semantics_->definitionOnly();scene.definitionExpanded=semantics_->definitionExpanded();scene.definitionOffset=semantics_->definitionOffset();if(auto selected=semantics_->selection())scene.selected=selected->id();scene.notice=semantics_->notice();scene.definition=semantics_->definition();scene.page=styledPage_;scene.semanticRegions=semanticVisual_.regions;scene.semanticCount=semanticVisual_.count;scene.semanticMenu=semanticVisual_.menu;scene.semanticPanel=semanticVisual_.panel;if(toolsView_.active())scene.tools=ToolsView::panel();if(menu_)scene.menu=menuRect_;if(notes_&&notes_->active()&&notes_->pickerVisible()){scene.palette=ib::notes::notePicker(notes_->view().note["expanded"].toBool());auto style=notes_->localStyle();scene.paletteStyle=1+unsigned(style.tool)+16*unsigned(style.width);}else if(tom_&&tom_->active()&&tom_->pickerVisible()){scene.palette=ib::notes::notePicker(false);auto style=tom_->localStyle();scene.paletteStyle=1+unsigned(style.tool)+16*unsigned(style.width)+64*unsigned(tom_->localEraser());}return scene;}
void Shell::coverUi(spike::Rect r)noexcept{for(unsigned i=0;i<pendingUiCount_;++i){const auto p=pendingUiRegions_[i];if(r.x0<=p.x0&&r.y0<=p.y0&&r.x1>=p.x1&&r.y1>=p.y1)pendingUiCovered_|=std::uint16_t(1u<<i);}if(pendingUiCount_&&pendingUiCovered_==std::uint16_t((1u<<pendingUiCount_)-1)){submittedUi_=pendingUi_;pendingUiCount_=0;pendingUiCovered_=0;}}
Change Shell::takeChange(){static_assert(RegionCount+8<=16);auto change=change_;change_={};pendingUi_=uiScene();pendingUiCount_=0;pendingUiCovered_=0;if(change.full)pendingUiRegions_[pendingUiCount_++]={0,0,1620,2160};else{const auto fixed=regions();for(unsigned i=0;i<fixed.size();++i)if(change.regions&(1u<<i))pendingUiRegions_[pendingUiCount_++]=fixed[i];for(unsigned i=0;i<change.extraCount;++i)pendingUiRegions_[pendingUiCount_++]=change.extra[i];}
 if(!change.full&&pendingUi_.page==submittedUi_.page){auto add=[&](spike::Rect r){r=semantic::clip(r);if(r.empty())return;if(change.uiCleanupCount==change.uiCleanup.size()){for(unsigned n=0;n<change.uiCleanupCount;++n)r.include(change.uiCleanup[n]);change.uiCleanupCount=0;}change.uiCleanup[change.uiCleanupCount++]=r;};auto same=[](spike::Rect a,spike::Rect b){return a.x0==b.x0&&a.y0==b.y0&&a.x1==b.x1&&a.y1==b.y1;};const bool semanticChanged=pendingUi_.definitionExpanded!=submittedUi_.definitionExpanded||pendingUi_.definitionOffset!=submittedUi_.definitionOffset||pendingUi_.definitionOnly!=submittedUi_.definitionOnly||pendingUi_.view!=submittedUi_.view||pendingUi_.selected!=submittedUi_.selected||pendingUi_.definition!=submittedUi_.definition||pendingUi_.notice!=submittedUi_.notice;
  if(semanticChanged){for(unsigned n=0;n<submittedUi_.semanticCount;++n)add(submittedUi_.semanticRegions[n]);add(pendingUi_.semanticMenu);add(pendingUi_.semanticPanel);}if(!same(pendingUi_.tools,submittedUi_.tools)){add(submittedUi_.tools);add(pendingUi_.tools);}if(!same(pendingUi_.menu,submittedUi_.menu)){add(submittedUi_.menu);add(pendingUi_.menu);}if(!same(pendingUi_.status,submittedUi_.status)||pendingUi_.statusStyle!=submittedUi_.statusStyle){add(submittedUi_.status);add(pendingUi_.status);}if(!same(pendingUi_.palette,submittedUi_.palette)||pendingUi_.paletteStyle!=submittedUi_.paletteStyle){add(submittedUi_.palette);add(pendingUi_.palette);}}
 return change;}
void Shell::semanticPaint(){for(unsigned i=0;i<semanticVisual_.count;++i)tomDamage(semanticVisual_.regions[i]);pageBackgrounds();semanticVisual_=semantic::transient({semanticOverlay_,1620,2160,1632},*semantics_,background(),{libraryMode_,bool(styled_),state_==State::Chrome?ib::reader::toolbarRect():spike::Rect{},epubZoomActive()?epubZoom_.scale():1,epubZoomActive()?epubZoom_.dx():0,epubZoomActive()?epubZoom_.dy():0});for(unsigned i=0;i<semanticVisual_.count;++i)tomDamage(semanticVisual_.regions[i]);revealSceneChanged();}
bool Shell::semanticAction(int x,int y){using namespace ib::reader;auto s=semantics_->selection();int id=semanticVisual_.targets.find(x,y);bool ok=false;
 auto refresh=[&]{if(semantics_->takeDirty())semanticPaint();};
 switch(id){
 case SelHighlight:if(styled_&&s)return openNotes(notes::Mode::Highlight,s);ok=semantics_->action(semantic::Action::Highlight);if(ok&&s)damage(contentRect(semantic::bounds(s->data)));refresh();return ok;
 case SelDefine:ok=semantics_->action(semantic::Action::Define);refresh();return ok;
 case DefMore:ok=semantics_->expandDefinition();refresh();return ok;
 case DefPrevious:ok=semantics_->scrollDefinition(-10);refresh();return ok;
 case DefNext:ok=semantics_->scrollDefinition(10);refresh();return ok;
 case SelNote:case DefWriteNote:if(styled_&&s)return openNotes(notes::Mode::Note,s);return false;
 case SelTom:case DefWriteTom:if(s&&libraryMode_)return openTom(s);return false;
 case SelCopy:if(s){sharedClipboard()=s->data["text"].toString();semantics_->dismiss();refresh();notice("Copied — paste it from the keyboard");}return true;
 case DefClose:semantics_->dismiss();refresh();return true;
 default:break;}
 if(semanticVisual_.menu.contains(x,y)||semanticVisual_.panel.contains(x,y))return true;
 semantics_->dismiss();refresh();return true;}
void Shell::reloadLibrary(){books_=library::documents(data_);books_+=library::documents(data_,true);}
// The page as currently displayed (document + ink, no chrome) for sheets to float over.
void Shell::backdrop(std::vector<std::uint32_t>& out){out.assign(underlay::Pixels,0xffffffffu);spike::Surface s{out,1620,2160,1632};if(styled_)styled_->composite(s,clean_,{0,0,1620,2160});else underlay::compose(s,clean_,ink(),{0,0,1620,2160});
 // A sheet inherits the visible reader scene, including opaque chrome. Copy
 // these pixels after page ink so the backdrop cannot expose hidden strokes.
 if(toolbarVisible()){paintChrome(true);for(auto rect:{ib::reader::toolbarRect(),ib::reader::closeRect()})for(int y=rect.y0;y<rect.y1;++y)std::copy_n(chrome_.data()+std::size_t(y)*1632+rect.x0,rect.x1-rect.x0,out.data()+std::size_t(y)*1632+rect.x0);}
}
void Shell::coverTick(){const auto arrived=covers_.collect();const bool showing=libraryMode_&&!document_&&state_==State::Picker&&!quickSettings_&&!wifiView_.active()&&!libraryManage_.active()&&!companionView_.active()&&!notebook_.active()&&lock_==Lock::None;if(!showing){covers_.cancel();return;}auto visible=libraryView_.visibleIds();if(!arrived.isEmpty()&&visible.contains(arrived)){const int index=visible.indexOf(arrived);libraryPaint();damage(libraryView_.targets().rect(ib::library::DocumentBase+index));}if(covers_.busy()||opener_.joinable()||companionBusy_||wifiBusy_)return;for(const auto& id:visible){const auto book=std::find_if(books_.begin(),books_.end(),[&](const auto& b){return b.id==id;});if(book!=books_.end()&&!covers_.find(id,book->sha256)&&covers_.request({app_,library::documentRoot(data_,id),id,book->sha256}))break;}}
void Shell::libraryRefresh(){
 if(change_.full||chrome_.size()!=underlay::Pixels){libraryPaint();change_.full=true;return;}
 libraryBefore_.assign(chrome_.begin(),chrome_.end());libraryPaint();if(change_.full)return;
 constexpr int W=1620,H=2160,S=underlay::Stride;auto same=[&](int y){return std::memcmp(&libraryBefore_[std::size_t(y)*S],&chrome_[std::size_t(y)*S],W*sizeof(std::uint32_t))==0;};
 for(int y=0;y<H;){if(same(y)){++y;continue;}
  // One band per run of changed rows; short unchanged gaps join the band so a popup stays one region.
  int y0=y,x0=W,x1=0;for(int gap=0;y<H&&gap<24;++y){if(same(y)){++gap;continue;}gap=0;const auto* a=&libraryBefore_[std::size_t(y)*S];const auto* b=&chrome_[std::size_t(y)*S];int l=0,r=W;while(l<r&&a[l]==b[l])++l;while(r>l&&a[r-1]==b[r-1])--r;x0=std::min(x0,l);x1=std::max(x1,r);}
  const int y1=std::min(H,y);damage({x0,y0,x1,y1});}
 // Area alone never authorizes the complete-update bit.
}
void Shell::libraryPaint(){libraryView_.setCoverSource([this](const QString& id,const QString& source){return covers_.find(id,source);});if(wifiView_.active()){wifiPaint();return;}libraryView_.setTimeZone(semantic::utf8(clockprefs::label(timeSetting_)));libraryView_.setClock(statusModel_.clock,statusModel_.zone);libraryView_.setBattery(battery_.percent.value_or(-1),battery_.charging(),wifiLink_.value_or(false));libraryView_.setDevice(battery_,wifiLink_,storage_,firmware_);libraryView_.setFavorites(favorites_);libraryView_.setEnvironment(libraryEffects_,companionEnabled_,true,userEnded_);{const auto& sleep=sleepSettings();libraryView_.setSleep(sleep.cover,sleep.idleMinutes);}libraryView_.setToolHandle(toolHandleShown());libraryView_.setLeftHanded(leftHanded());libraryView_.setReading(readingProgress(),snapHighlights());
 {const auto state=companionStatus_["state"].toString();const int companion=!companionEnabled_?-1:state=="paired"||state=="transferring"||state=="transfer-error"?2:state=="pairing"?1:state=="unpaired"?0:3;
  libraryView_.setPreferences(passcodeSet(),companion,prefs_.highContrast,prefs_.reduceMotion,prefs_.tomTyped,prefs_.tomHand,prefs_.tomReveal,tomNetworkEnabled_?1:0,prefs_.plainText);}
 if(libraryView_.notice().isEmpty())try{auto failures=QJsonDocument::fromJson(underlay::readBounded(data_/"import-errors.json",16384)).array();if(!failures.isEmpty())setLibraryNotice("Some files were rejected — your originals are kept");}catch(...){}
 libraryView_.paint({chrome_,underlay::Width,underlay::Height,underlay::Stride},books_,data_);}
// Library-wide notes, read-only: verified feature stores of each book; never creates a store.
void Shell::libraryNotes(){QVector<LibraryView::NoteEntry> out;
 for(const auto& d:books_){if(d.trashed)continue;try{auto path=library::documentRoot(data_,d.id)/"features"/"features.json";if(!std::filesystem::exists(path))continue;
   auto wrapper=QJsonDocument::fromJson(underlay::readBounded(path,16*1024*1024)).object();auto payload=wrapper["payload"].toObject();
   if(product::sha256(QJsonDocument(payload).toJson(QJsonDocument::Compact))!=wrapper["sha256"].toString())continue;
   for(auto v:payload["annotation"].toObject()["notes"].toArray()){auto n=v.toObject();LibraryView::NoteEntry e;e.document=d.id;e.noteId=n["id"].toString();e.anchor=n["anchor"].toObject();
    e.excerpt=e.anchor["excerpt"].toString().simplified().left(400);e.body=n["text"].toString().simplified().left(400);e.linked=!n["highlight"].toString().isEmpty();e.handwriting=!n["strokes"].toArray().isEmpty();
    e.title=n["title"].toString();e.tom=e.title.startsWith("Tom");if(e.title.isEmpty())e.title=e.linked?"Note on a highlight":"Page note";
    e.page=e.anchor["page"].toInt(-1);e.source=(e.page>=0?QString("p. %1 · ").arg(e.page+1):QString())+d.title;e.created=QDateTime::fromString(n["created"].toString(),Qt::ISODateWithMs).toMSecsSinceEpoch();
    for(unsigned i=1;i<8;++i)if(n["color"].toString()==styled::colorId(i))e.color=i;e.favorite=noteFavorites_.contains(e.noteId);out.push_back(std::move(e));}}catch(...){}}
 std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.created>b.created;});libraryView_.setNotes(std::move(out));}
bool Shell::startImport(){if(opener_.joinable())return false;if(!preparationTracking_.accept(openerTracking_))return false;done_=false;openFailed_=false;syncing_=true;state_=State::Loading;setLibraryNotice("Importing from Inbox…");libraryPaint();change_.full=true;startOpener([this]{try{openerReconciled_=false;reader::syncLibrary(app_,data_,stop_);openerReconciled_=true;}catch(const std::exception& e){if(reader::lifecycle::reconciledHelperFailure(e))openerReconciled_=true;openFailed_=true;}catch(...){openFailed_=true;}done_=true;});return true;}
bool Shell::openNewNotebook(){if(timezoneView_.active()||!libraryMode_||state_!=State::Picker||opener_.joinable())return false;notebook_.open(libraryView_.folder());notebook_.paint({chrome_,1620,2160,1632});change_.full=true;return true;}
void Shell::saveFavorites(){QJsonArray ids;for(const auto& id:favorites_)ids.append(id);try{underlay::atomicFile(data_/"ui-favorites.json",QJsonDocument(QJsonObject{{"version",1},{"ids",ids}}).toJson(QJsonDocument::Compact));}catch(...){setLibraryNotice("Favorites could not be saved");}}
bool Shell::goHome(){if(stop_||!libraryMode_||lock_!=Lock::None||timezoneView_.active()||quickSettings_||wifiView_.active()||libraryManage_.active()||companionView_.active()||notebook_.active()||viewControls_.active()||(tom_&&tom_->active())||(notes_&&notes_->active()))return false;if(document_){if(!returnToLibrary())return false;libraryView_.showSection(LibraryView::Section::MyFiles);return true;}if(state_!=State::Picker)return false;libraryView_.showSection(LibraryView::Section::MyFiles);libraryRefresh();return true;}
bool Shell::returnToLibrary(){if(stop_)return false;if((tom_&&tom_->active())||!libraryMode_||!document_||document_->contact()||(notes_&&notes_->active())||(styled_&&styled_->status().busy))return false;clearNavigation();returning_=true;state_=State::NavigationPending;document_->request(reader::Action::Exit);return true;}
/* Timing values encode (operation << 10) | target; a page is at most 999. These are software events, never optical completion. */
void Shell::clearNavigation(){viewportRefresh_=false;if(navigationActiveOperation_)recovery::v2::e2::mark("ui:nav-cancelled",(navigationActiveOperation_<<10)|unsigned(std::max(0,navigationSubmitted_)));if(navigationOperation_&&navigationOperation_!=navigationActiveOperation_)recovery::v2::e2::mark("ui:nav-cancelled",(navigationOperation_<<10)|unsigned(std::max(0,navigationIntent_)));navigationIntent_=navigationSubmitted_=-1;navigationOperation_=navigationActiveOperation_=0;navigationRetryAt_={};}
bool Shell::dispatchNavigation(){if(stop_||returning_||changingView_||!document_||navigationIntent_<0||document_->contact()||document_->state()!=reader::State::Active||!inkSettled()||toolsView_.active()||viewControls_.active()||(tom_&&tom_->active())||(notes_&&notes_->active())||std::chrono::steady_clock::now()<navigationRetryAt_)return false;
 const auto value=(navigationOperation_<<10)|unsigned(navigationIntent_);if(navigationIntent_==page()){recovery::v2::e2::mark("ui:nav-satisfied",value);navigationIntent_=-1;navigationOperation_=0;if(state_==State::NavigationPending){state_=selectMode_&&selectionChrome_?State::Chrome:State::Clean;pageBackgrounds();change_.regions|=ChromeRegions;}return true;}
 if(!document_->navigateTo(navigationIntent_)){auto refusal=document_->takeNavigationRefusal();if(refusal==reader::NavigationRefusal::Busy){navigationRetryAt_=std::chrono::steady_clock::now()+std::chrono::milliseconds(25);return false;}recovery::v2::e2::mark("ui:nav-failed",value);navigationIntent_=-1;navigationOperation_=0;state_=selectMode_&&selectionChrome_?State::Chrome:State::Clean;notice("That page isn\u2019t available \u2014 your data is kept");return false;}
 navigationSubmitted_=navigationIntent_;navigationActiveOperation_=navigationOperation_;recovery::v2::e2::mark("ui:nav-dispatched",value);state_=State::NavigationPending;return true;}
bool Shell::navigateTo(int target){const auto operation=++navigationSequence_;const auto value=(operation<<10)|unsigned(std::clamp(target,0,1023));recovery::v2::e2::mark("ui:nav-arrival",value);
 if(stop_||returning_||changingView_||!document_||document_->contact()||toolsView_.active()||(tom_&&tom_->active())||target<0||target>=document_->total()||(state_!=State::Clean&&state_!=State::Chrome&&state_!=State::NavigationPending)||(target==page()&&navigationIntent_<0)){recovery::v2::e2::mark("ui:nav-refused",value);return false;}
 if(navigationOperation_&&navigationOperation_!=navigationActiveOperation_)recovery::v2::e2::mark("ui:nav-superseded",(navigationOperation_<<10)|unsigned(navigationIntent_));navigationIntent_=target;navigationOperation_=operation;navigationRetryAt_={};recovery::v2::e2::mark("ui:nav-accepted",value);penSelect_={};penRangeCount_=0;penRangeOverflow_=false;semantics_->cancel();if(semantics_->takeDirty())semanticPaint();state_=State::NavigationPending;(void)dispatchNavigation();
 /* An immediate refusal clears the intent and shows its notice: report it, so pending note and Tom jumps stop
    instead of asking again every tick. Deferred (busy) and dispatched requests keep their intent. */
 return navigationIntent_==target||navigationSubmitted_==target||page()==target;}
std::array<spike::Rect,2> Shell::selectionHandles()const{std::array<spike::Rect,2> result{};if((tom_&&tom_->active())||(notes_&&notes_->active())||!selectionEnabled()||semantics_->view()!=semantic::View::Menu)return result;const auto selected=semantics_->selection();if(!selected)return result;const auto quads=selected->data["quads"].toArray(),matrix=selected->data["transform"].toArray();if(quads.isEmpty()||matrix.size()!=6)return result;auto point=[&](double x,double y){return std::pair{matrix[0].toDouble()*x+matrix[2].toDouble()*y+matrix[4].toDouble(),matrix[1].toDouble()*x+matrix[3].toDouble()*y+matrix[5].toDouble()};};auto first=quads.first().toArray(),last=quads.last().toArray();if(first.size()!=8||last.size()!=8)return result;auto a=point(first[0].toDouble()+1,(first[1].toDouble()+first[5].toDouble())/2),z=point(last[2].toDouble()-1,(last[3].toDouble()+last[7].toDouble())/2);if(epubZoomActive()){a=epubZoom_.map(a.first,a.second);z=epubZoom_.map(z.first,z.second);}auto bounds=[](auto p){return spike::Rect{std::max(0,int(p.first)-72),std::max(0,int(p.second)-72),std::min(1620,int(p.first)+73),std::min(2060,int(p.second)+73)};};result[0]=bounds(a);result[1]=bounds(z);return result;}
reader::DragMode Shell::touchMode()const noexcept{using M=reader::DragMode;if(!selectionEnabled())return M::None;if(tom_&&tom_->active())return M::Scroll;if(notes_&&notes_->active())return M::Ink;if(selectMode_||semantics_->view()==semantic::View::Menu)return M::Text;return M::Highlight;}
bool Shell::touchDrag(const reader::Touch::Tap& t){if(!t.valid||!t.drag||t.cancelled||t.epoch!=version()||t.mode==reader::DragMode::None||t.mode!=touchMode())return false;auto a=contentPoint(t.startX,t.startY),b=contentPoint(t.x,t.y);return selectionDrag(a.first,a.second,b.first,b.second,t.epoch);}
bool Shell::selectionDrag(int sx,int sy,int ex,int ey,std::uint64_t epoch){if(quickSettings_||wifiView_.active()||(libraryMode_&&statusBarRect_.contains(sx,sy)))return false;if(stop_||lock_!=Lock::None)return false;if(tom_&&tom_->active())return epoch==version()&&tomGestureStamp_==tom_->gestureEpoch()&&tom_->pan(sx,sy,ex,ey);if(epoch!=version()||!selectionEnabled()||document_->contact())return false;if(notes_&&notes_->active())return notes_->selectionDrag(sx,sy,ex,ey);if(state_==State::Chrome&&(topStrip().contains(sx,sy)||ib::reader::toolbarRect().contains(sx,sy)))return false;const bool adjusting=semantics_->view()==semantic::View::Menu;if(adjusting&&(semanticVisual_.menu.contains(sx,sy)||semanticVisual_.panel.contains(sx,sy)))return false;auto result=selectRange(sx,sy,ex,ey,selectMode_,false);
 if(result.selection){if(adjusting||selectMode_){if(!semantics_->select(result.selection,!result.replace)){notice("Select fewer or shorter passages");return false;}if(semantics_->takeDirty())semanticPaint();return true;}return openNotes(notes::Mode::Highlight,result.selection);}
 if(adjusting)return false;semantics_->notify(QString::fromUtf8(semantic::rangeStatusText(result.status)));semanticPaint();return false;}
bool Shell::finger(int x,int y,bool longPress,bool markerContact){if(lock_!=Lock::None)return !markerContact&&!longPress?lockTap(x,y):true;
 if(stop_)return false;if(timezoneView_.active())return !markerContact&&!longPress?timezoneFinger(x,y):true;
 if(document_&&!notice_.isEmpty()&&!markerContact&&!document_->contact()&&!(tom_&&tom_->active())&&!(notes_&&notes_->active())){notice_.clear();paintChrome();change_.regions|=4;}
 if(viewControls_.active()){if(markerContact||(document_&&document_->contact())||longPress)return false;auto command=viewControls_.hit(x,y,viewSnapshot());using A=DocumentControls::Action;
  if(command.action==A::Cancel){viewControls_={};pageBackgrounds();tomDamage(ib::outset(DocumentControls::Panel,4));revealSceneChanged();return true;}
  if(command.action==A::JumpPage){if(command.bookmarkIndex>=0){if(!jumpBookmark(command.bookmarkIndex)){viewControls_.rejectApply();viewPaint(false);}return true;}int target=command.targetPage;viewControls_={};if(target!=page())navigateTo(target);else pageBackgrounds();tomDamage(ib::outset(DocumentControls::Panel,4));revealSceneChanged();return true;}
  if(command.action==A::Quiet&&styled_){if(!styled_->setRevealEnabled(command.quietEffects))viewControls_.rejectApply();}
  if(command.action==A::ApplyPdf){if(!applyPdfView(command.pdf))viewControls_.rejectApply();}else if(command.action==A::ApplyEpub){if(!applyEpubView(command.epub))viewControls_.rejectApply();}
  if(command.consumed&&viewControls_.active())viewPaint(false);return command.consumed;}
 if(tom_&&tom_->active()){if(markerContact||tom_->contact()||longPress)return false;bool used=tom_->finger(x,y);tomTick();return used;}
 if(libraryMode_&&(state_==State::Picker||document_)&&!markerContact&&(wifiView_.active()||wifiPassword_.active())){if(longPress)return false;
  if(wifiPassword_.active()){wifiPassword_.finger(x,y);if(wifiPassword_.done()){auto password=wifiPassword_.text();wifiPassword_.acknowledge();if(wifiChosen_)runWifi(wifiChosen_,password,false);}else if(wifiPassword_.cancelled())wifiPassword_.acknowledge();wifiPaint();return true;}
  auto hit=wifiView_.hit(x,y);using W=WifiView::Action;
  if(hit.action==W::Close){wifiView_.close();if(document_){pageBackgrounds();quickSettings_=true;refreshStatus(true);}else libraryPaint();change_.full=true;return true;}
  if(hit.action==W::Rescan){startWifi(-1,{});wifiPaint();return true;}
  if(hit.action==W::Select){const auto& net=wifiView_.networks()[std::size_t(hit.index)];wifiSelected_=hit.index;wifiChosen_=net;if(net.savedId>=0||net.security==wifi::Security::Open)runWifi(net,{},false);else wifiPassword_.open({},"Password for "+net.ssid.left(40),63,true);wifiPaint();return true;}
  return false;}
 if(!markerContact&&!(notes_&&notes_->active())&&!(tom_&&tom_->active())){if(longPress&&statusBarRect_.contains(x,y))return true;if(!longPress&&statusFinger(x,y))return true;}
 if(notes_&&notes_->active()){if(notesClosing_||markerContact||document_->contact()||longPress)return false;bool hit=notes_->finger(x,y);notesTick();return hit;}
 // Pen panel: its own cells, the rail beside it (eraser/undo/redo route through the panel), or a tap elsewhere closes it.
 if(styled_&&toolsView_.active()){if(markerContact||document_->contact()||longPress)return false;
  for(auto shortcut:{ib::reader::RailPen,ib::reader::RailHighlighter}){const unsigned slot=shortcut==ib::reader::RailPen?0u:1u;if(slot!=activeShortcut_&&chromeTargets_.rect(shortcut).contains(x,y)){toolsView_={};return railAction(shortcut);}}
  bool panelTarget=ToolsView::panel().contains(x,y)||toolsView_.closeRect().contains(x,y)||ToolsView::eraser().contains(x,y)||ToolsView::undo().contains(x,y)||ToolsView::redo().contains(x,y);
  if(!panelTarget){int id=chromeTargets_.find(x,y);if(id>=ib::reader::RailPen&&id<=ib::reader::RailMore)return railAction(id);toolsView_={};toolsPaint();return true;}
  auto command=toolsView_.hit(x,y,toolsSnapshot());if(command.refusal!=ToolsView::Refusal::None)recovery::v2::e2::mark("ui:tool-refusal",unsigned(command.refusal));bool ok=false;using A=ToolsView::Action;
  switch(command.action){case A::Snap:setSnapHighlights(command.snapHighlights);ok=true;break;case A::Tool:ok=styled_->selectTool(styled::Tool(command.style.tool));if(ok)selectMode_=false;break;case A::Width:ok=styled_->selectWidth(styled::Width(command.style.width));break;case A::Color:ok=styled_->selectColor(command.style.color);break;case A::Eraser:ok=styled_->setEraser(command.style.eraser);break;case A::Undo:ok=styled_->undo();if(ok)expectRemoval();break;case A::Redo:ok=styled_->redo();if(ok)expectRemoval();break;case A::Reveal:ok=styled_->setRevealEnabled(command.quietEffects);break;case A::Close:ok=true;break;default:break;}
  if(ok&&(command.action==A::Tool||command.action==A::Width||command.action==A::Color))rememberShortcut();
  if(command.action!=A::None){recovery::v2::e2::mark(ok?"ui:tool-command-admitted":"ui:tool-command-refused",unsigned(command.action));++toolsRevision_;if(toolsView_.active())toolsView_.update(toolsSnapshot());toolsPaint();}return ok||command.consumed;}
 if(libraryMode_&&state_==State::Picker&&!markerContact){
  if(notebook_.active()){if(longPress)return false;using N=NotebookView::Action;auto act=notebook_.hit(x,y);
   if(act==N::Close){if(pendingTomNote_["waitNotebook"].toBool())pendingTomNote_={};libraryPaint();change_.full=true;return true;}
   if(act==N::Import){if(pendingTomNote_["waitNotebook"].toBool())pendingTomNote_={};notebook_.close();return startImport();}
   if(act==N::Create){if(opener_.joinable()||!preparationTracking_.accept(openerTracking_))return false;
    // Generate the template PDF in app-private storage, then import it like any document.
    QString name=notebook_.title();name.replace(QRegularExpression("[^\\p{L}\\p{N} _()\\-]")," ");name=name.simplified().left(80);if(name.isEmpty())name="Notebook";
    std::filesystem::path file;try{underlay::privateDirectory(data_/"notebooks");file=data_/"notebooks"/(name.toStdString()+".pdf");for(int n=2;std::filesystem::exists(file)&&n<1000;++n)file=data_/"notebooks"/(name.toStdString()+" "+std::to_string(n)+".pdf");
     auto bytes=notebook::pdf(notebook_.paper(),notebook_.pages());underlay::atomicFile(file,QByteArray(bytes.data(),qsizetype(bytes.size())));}catch(...){preparationTracking_.complete(openerTracking_,true);notebook_.setBusy(false,"The notebook could not be written");notebook_.paint({chrome_,1620,2160,1632});change_.full=true;return true;}
    idsBeforeImport_.clear();for(const auto& d:books_)idsBeforeImport_.insert(d.id);notebookFolder_=notebook_.folder();creatingNotebook_=true;
    notebook_.setBusy(true,"Creating notebook…");notebook_.paint({chrome_,1620,2160,1632});change_.full=true;done_=false;openFailed_=false;syncing_=true;state_=State::Loading;
    startOpener([this,file]{try{openerReconciled_=false;reader::importFile(app_,data_,file,stop_);openerReconciled_=true;}catch(const std::exception& e){if(reader::lifecycle::reconciledHelperFailure(e))openerReconciled_=true;openFailed_=true;}catch(...){openFailed_=true;}done_=true;});return true;}
   notebook_.paint({chrome_,1620,2160,1632});change_.full=true;return true;}
  if(companionView_.active()){if(longPress)return false;auto hit=companionView_.hit(x,y);using A=CompanionView::Action;if(hit.action==A::Close){libraryPaint();change_.full=true;return true;}
   QString action;switch(hit.action){case A::StartPair:action="pair";break;case A::PollPair:action="poll";break;case A::Approve:action="approve";break;case A::Reject:action="reject";break;case A::Sync:action="sync";break;case A::RetryTransfer:action="retry";break;case A::Unpair:action="unpair";break;default:break;}
   if(!action.isEmpty()&&!startCompanion(action,hit.expectedPairId,hit.expectedAccount))companionView_.refused();companionView_.paint({chrome_,1620,2160,1632});change_.full=true;return hit.action!=A::None;}
  if(libraryManage_.active()){if(longPress)return false;auto command=libraryManage_.hit(x,y);
   try{if(command.command==LibraryManageCommand::Favorite){if(favorites_.contains(command.id))favorites_.remove(command.id);else favorites_.insert(command.id);saveFavorites();}
    else if(command.command!=LibraryManageCommand::None&&command.command!=LibraryManageCommand::Changed&&command.command!=LibraryManageCommand::Cancel){library::Store store(data_);switch(command.command){case LibraryManageCommand::Rename:store.rename(command.id,command.text);break;case LibraryManageCommand::Folder:store.organize(command.id,command.text);break;case LibraryManageCommand::Trash:store.trash(command.id,true);break;case LibraryManageCommand::Restore:store.trash(command.id,false);break;case LibraryManageCommand::Delete:store.permanentlyDelete(command.id,true);if(favorites_.remove(command.id))saveFavorites();break;default:break;}reloadLibrary();}
    libraryManage_.setFavorite(favorites_.contains(libraryManage_.document()));libraryPaint();if(libraryManage_.active())libraryManage_.paint({chrome_,1620,2160,1632});}
   catch(...){setLibraryNotice("The change failed — reopen the library to reconcile");libraryPaint();}change_.full=true;return command.command!=LibraryManageCommand::None;}
  if(longPress){auto hit=libraryView_.hit(x,y);if(hit.action==LibraryView::Action::Open&&hit.index>=0){libraryManage_.open(books_[hit.index]);libraryManage_.setFavorite(favorites_.contains(books_[hit.index].id));libraryManage_.paint({chrome_,1620,2160,1632});change_.full=true;return true;}if(hit.action==LibraryView::Action::Changed){libraryPaint();change_.full=true;}return false;}
  const bool previousGrid=libraryView_.grid();auto hit=libraryView_.hit(x,y);using LA=LibraryView::Action;
  switch(hit.action){
  case LA::Changed:if(previousGrid!=libraryView_.grid()){libraryPaint();change_.full=true;recovery::v2::e2::mark("ui:library-view-quality",libraryView_.grid()?1u:0u);}else libraryRefresh();return true;
  case LA::NotesChanged:libraryNotes();libraryRefresh();return true;
  case LA::NoteFavorite:{const auto* n=libraryView_.note(hit.note);if(!n||!noteFavorites_.toggle(n->noteId))setLibraryNotice("The star could not be saved");libraryNotes();libraryRefresh();return true;}
  case LA::Open:if(books_[hit.index].trashed){libraryManage_.open(books_[hit.index]);libraryManage_.paint({chrome_,1620,2160,1632});change_.full=true;return true;}return choose(unsigned(hit.index));
  case LA::OpenNote:{const auto* n=libraryView_.note(hit.note);if(!n)return false;for(int i=0;i<books_.size();++i)if(books_[i].id==n->document&&!books_[i].trashed){pendingNote_={n->document,n->noteId,n->anchor};if(choose(unsigned(i)))return true;pendingNote_={};}notice("That note's book is unavailable");return false;}
  case LA::Tom:return openTom();
  case LA::Shared:{if(!companionEnabled_){notice("The companion is offline — Inbox imports continue automatically");return true;}auto status=companionStatus_;status["busy"]=companionBusy_;companionView_.open(status);companionView_.paint({chrome_,1620,2160,1632});change_.full=true;return true;}
  case LA::Import:return startImport();
  case LA::NewNotebook:return openNewNotebook();
  case LA::Effects:libraryEffects_=!libraryEffects_&&!prefs_.reduceMotion;libraryRefresh();revealSceneChanged();return true;
  case LA::Exit:if(userEnded_){notice("Press the power button to leave Inkie");return true;}return action(Action::Exit);
  case LA::ToolHandle:setToolHandle(!toolHandleShown());libraryRefresh();return true;
  case LA::LeftHanded:setLeftHanded(!leftHanded());libraryRefresh();return true;
  case LA::ReadingProgress:setReadingProgress(!readingProgress());libraryRefresh();return true;
  case LA::TomStyle:prefs_.tomTyped=hit.index==1;savePrefs();libraryRefresh();return true;
  case LA::TomHand:prefs_.tomHand=unsigned(std::clamp(hit.index,0,2));savePrefs();libraryRefresh();return true;
  case LA::TomReveal:if(!prefs_.reduceMotion){prefs_.tomReveal=!prefs_.tomReveal;savePrefs();}libraryRefresh();return true;
  case LA::HighContrast:prefs_.highContrast=!prefs_.highContrast;applyPrefs();savePrefs();libraryRefresh();return true;
  case LA::PlainText:prefs_.plainText=!prefs_.plainText;applyPrefs();savePrefs();libraryRefresh();return true; /* glyphs change inside unchanged rects: the same regional diff */
  case LA::ReduceMotion:prefs_.reduceMotion=!prefs_.reduceMotion;applyPrefs();savePrefs();libraryRefresh();return true;
  case LA::PasscodeSet:lockThenChoose_=passcodeSet();beginLock(lockThenChoose_?Lock::Verify:Lock::Choose);return true;
  case LA::PasscodeOff:if(passcodeSet()){lockThenChoose_=false;beginLock(Lock::Verify);}return true;
  case LA::SleepCover:case LA::SleepIdle:{auto sleep=sleepSettings();if(hit.action==LA::SleepCover)sleep.cover=!sleep.cover;else sleep.idleMinutes=hit.index;if(!setSleepSettings(sleep))setLibraryNotice("Sleep setting not saved");libraryRefresh();return true;}
  case LA::TimeZone:timezoneView_.open(timeSetting_.zone);timezonePaint();++presentedVersion_;return true;
  case LA::Wifi:if(!userEnded_)return false;wifiView_.open();wifiPaint();startWifi(-1,{});return true; /* D2: only power ends a user-ended session; its leave slot is Wi-Fi */
  default:return false;}
 }
 if(markerContact||(document_&&document_->contact()))return false;
 if(document_&&menu_&&!longPress){int id=menuTargets_.find(x,y);if(id>=0&&quickBrowsing())return quickBrowseTap(id,x);if(id>=0)return menuAction(id);menu_=false;pageBackgrounds();damage(menuRect_);damage(ib::reader::toolbarSlot(ib::reader::SlotDocument));return true;}
 if(document_&&semantics_->view()!=semantic::View::None&&!longPress){if(state_==State::Chrome&&!semanticVisual_.menu.contains(x,y)&&!semanticVisual_.panel.contains(x,y)){const int id=chromeTargets_.find(x,y);if(id==ib::reader::ToolHandle){const bool hidden=action(Action::Hide);if(hidden)semanticPaint();return hidden;}if(id>=ib::reader::RailPen&&id<=ib::reader::RailMore){semantics_->dismiss();if(semantics_->takeDirty())semanticPaint();return railAction(id);}}if(selectMode_&&!semanticVisual_.menu.contains(x,y)&&!semanticVisual_.panel.contains(x,y)&&semantics_->longPress(contentPoint(x,y).first,contentPoint(x,y).second,QString::fromLatin1(document_->active().prepared.binding.toHex()))){if(semantics_->takeDirty())semanticPaint();return true;}return semanticAction(x,y);}
 if(document_&&!longPress&&state_==State::Chrome){int id=chromeTargets_.find(x,y);
  if(id>=ib::reader::RailPen&&id<=ib::reader::RailMore)return railAction(id);
  if(id==ib::reader::ToolHandle)return action(Action::Hide); /* the handle collapses the toolbar */
  if(id==ib::reader::ZoomBadge)return resetZoom();}
 if(styled_&&!longPress&&!toolsView_.active())for(const auto& note:noteIndicators_)if(ib::hitArea(contentRect(note.bounds)).contains(x,y))return openNotes(notes::Mode::Note,{},note.id); /* 96 px target around the 42 px marker */
 if(longPress){if(!document_||!document_->writable()||(styled_&&(!styled_->status().ready||styled_->status().failed))||(state_==State::Chrome&&(topStrip().contains(x,y)||ib::reader::toolbarRect().contains(x,y))))return false;if(state_==State::Chrome&&!selectMode_){state_=State::Clean;change_.regions|=ChromeRegions;}bool selected=semantics_->defineAt(contentPoint(x,y).first,contentPoint(x,y).second,QString::fromLatin1(document_->active().prepared.binding.toHex()));if(semantics_->takeDirty())semanticPaint();return selected;}
 if(selectMode_&&styled_&&!longPress&&(state_==State::Clean||state_==State::Chrome)&&y<2060&&!(state_==State::Chrome&&(topStrip().contains(x,y)||ib::reader::toolbarRect().contains(x,y)))){bool selected=semantics_->longPress(contentPoint(x,y).first,contentPoint(x,y).second,QString::fromLatin1(document_->active().prepared.binding.toHex()));if(semantics_->takeDirty())semanticPaint();return selected;}
 if(styled_&&!selectMode_&&styled_->style().tool==styled::Tool::Highlighter&&state_==State::Clean&&semantics_->view()==semantic::View::None&&y<2060){if(semantics_->longPress(contentPoint(x,y).first,contentPoint(x,y).second,QString::fromLatin1(document_->active().prepared.binding.toHex()))){auto selected=semantics_->selection();return openNotes(notes::Mode::Highlight,selected);}}
 if(document_&&state_==State::Clean&&styled_&&cleanTargets_.find(x,y)==ib::reader::ToolHandle)return action(Action::Show); /* the collapsed toolbar opens it */
 if(document_&&!longPress&&(state_==State::Clean||state_==State::Chrome)&&(state_==State::Clean?cleanTargets_:chromeTargets_).find(x,y)==ib::reader::ZoomBadge)return resetZoom();
 {auto a=hit(state_,x,y);if(a==Action::Exit&&libraryMode_&&document_&&state_==State::Chrome)return returnToLibrary();
  /* The close is drawn while a page turns too: remember the tap and leave once the page settles (never quit). */
  if(a==Action::Exit&&libraryMode_&&document_&&state_==State::NavigationPending&&!returning_){exitAfterNavigation_=true;return true;}if(userEnded_&&a==Action::Exit)return false; /* D2: never ends the session */return action(a,markerContact);}
}
// Pinch and two-finger pan (PDF): applied at lift as one view change through the qualified PDF view path (one crisp
// re-render, no e-ink update storm mid-gesture). Zoom stays about the focal point; pan stays within the page.
// Device status (read-only, sysfs): at most once a minute and never during a contact. The library header
// refreshes only its status region when the value changes; the reader shows no battery (as the native reader).
void Shell::pollDevice(){const auto now=std::chrono::steady_clock::now();if(batteryAt_!=std::chrono::steady_clock::time_point{}&&now-batteryAt_<std::chrono::seconds(60))return;batteryAt_=now;
 if(firmware_.empty())firmware_=device::firmware();
 const auto next=device::readBattery();const auto link=device::wifiLink();const auto storage=libraryMode_?device::readStorage(data_):device::Storage{};if(next==battery_&&link==wifiLink_&&storage==storage_)return;battery_=next;wifiLink_=link;storage_=storage;
 if(libraryMode_&&!document_&&state_==State::Picker&&!wifiView_.active()&&!libraryManage_.active()&&!companionView_.active()&&!notebook_.active()){libraryPaint();damage(ib::library::StatusRect);
  // On Settings, the readings sit in the first rows of the pane (Battery, Wi-Fi, Storage): refresh that band only.
  if(libraryView_.section()==ib::library::Section::Settings)damage({ib::settings::PaneRect.x0,ib::settings::PaneRect.y0,ib::settings::PaneRect.x1,900});}
 if(battery_.percent&&*battery_.percent<=10&&!battery_.charging()){if(!lowBatteryShown_){lowBatteryShown_=true;notice(QString("Battery low \u2014 %1%").arg(*battery_.percent));}}else lowBatteryShown_=false;}

void Shell::timezonePaint(){std::copy(chrome_.begin(),chrome_.end(),viewPixels_.begin());timezoneView_.paint({viewPixels_,1620,2160,1632});change_.full=true;}
bool Shell::timezoneFinger(int x,int y){if(!timezoneView_.finger(x,y))return false;if(auto choice=timezoneView_.takeChoice()){clockprefs::Setting next{*choice};if(clockprefs::save(data_,next)){timeSetting_=std::move(next);clockMinute_=-1;pollClock(QDateTime::currentSecsSinceEpoch());}else setLibraryNotice("The time zone could not be saved — your previous setting is kept");}if(timezoneView_.active())timezonePaint();else{libraryPaint();change_.full=true;++presentedVersion_;}return true;}
void Shell::pollClock(qint64 seconds){const qint64 minute=seconds/60;if(minute==clockMinute_)return;clockMinute_=minute;const auto time=clockprefs::at(seconds,timeSetting_);statusModel_.clock=semantic::utf8(time.time);statusModel_.zone=semantic::utf8(time.abbreviation);refreshStatus(true);}
void Shell::refreshStatus(bool force){if(!libraryMode_)return;auto model=statusModel_;model.battery=battery_.percent.value_or(-1);model.charging=battery_.charging();model.wifi=wifiLink_?(*wifiLink_?1:0):-1;auto light=light_.view();model.light=light.available?(light.power==4?0:light.requested):-1;model.maximum=light.available?light.maximum:-1;model.busy=light.changing;model.error=semantic::utf8(light.error);model.clockOnly=bool(document_);auto rect=document_?ib::status::ReaderClock:ib::status::LibraryBar;const bool same=rect.x0==statusBarRect_.x0&&rect.y0==statusBarRect_.y0&&rect.x1==statusBarRect_.x1&&rect.y1==statusBarRect_.y1;if(!force&&same&&model==statusModel_)return;if(!force&&same&&!panelShown()){auto bar=model;bar.light=statusModel_.light;bar.maximum=statusModel_.maximum;bar.busy=statusModel_.busy;bar.error=statusModel_.error;if(bar==statusModel_){statusModel_=std::move(model);return;}} /* light fields are drawn only in the panel */statusModel_=std::move(model);if(wifiView_.active())return;statusBarRect_=rect;statusTargets_.clear();ib::Canvas canvas({viewPixels_,1620,2160,1632});ib::status::bar(canvas,statusTargets_,statusModel_,rect);statusBarPixels_.resize(std::size_t(rect.x1-rect.x0)*(rect.y1-rect.y0));for(int y=rect.y0;y<rect.y1;++y)std::copy_n(viewPixels_.data()+std::size_t(y)*1632+rect.x0,rect.x1-rect.x0,statusBarPixels_.data()+std::size_t(y-rect.y0)*(rect.x1-rect.x0));if(panelShown())ib::status::panel(canvas,statusTargets_,statusModel_);damage(rect);if(panelShown())damage(ib::status::Panel);}
void Shell::compositeStatus(spike::Surface out,spike::Rect damage)const noexcept{if(!libraryMode_||timezoneView_.active()||wifiView_.active()||libraryManage_.active()||companionView_.active()||notebook_.active()||lock_!=Lock::None||(!document_&&state_!=State::Picker))return;auto r=ib::intersect(statusBarRect_,damage);for(int y=r.y0;y<r.y1;++y)std::copy_n(statusBarPixels_.data()+std::size_t(y-statusBarRect_.y0)*(statusBarRect_.x1-statusBarRect_.x0)+r.x0-statusBarRect_.x0,r.x1-r.x0,out.pixels.data()+std::size_t(y)*out.stridePixels+r.x0);if(panelShown()){r=ib::intersect(ib::status::Panel,damage);for(int y=r.y0;y<r.y1;++y)std::copy_n(viewPixels_.data()+std::size_t(y)*1632+r.x0,r.x1-r.x0,out.pixels.data()+std::size_t(y)*out.stridePixels+r.x0);}}
bool Shell::openQuickSettings(){if(!statusAvailable()||viewControls_.active()||(notes_&&notes_->active())||(tom_&&tom_->active())||libraryManage_.active()||companionView_.active()||notebook_.active()||(document_&&(document_->contact()||(state_!=State::Clean&&state_!=State::Chrome&&state_!=State::NavigationPending))))return false;toolsView_={};menu_=false;if(document_){semantics_->dismiss();semanticPaint();}quickSettings_=true;const auto light=light_.view();const bool unknown=!light.available&&light.error.isEmpty();if((light_.request(app_)||light.busy)&&unknown){quickSettingsWait_=true;quickSettingsUntil_=std::chrono::steady_clock::now()+std::chrono::milliseconds(400);return true;} /* first open since launch: the panel is painted once, when its level is known */refreshStatus(true);damage(ib::status::Panel);++presentedVersion_;return true;}
bool Shell::statusFinger(int x,int y){if(!libraryMode_||stop_||lock_!=Lock::None||wifiView_.active()||viewControls_.active()||(document_&&document_->contact())||(tom_&&tom_->active())||(notes_&&notes_->active())||libraryManage_.active()||companionView_.active()||notebook_.active())return false;namespace S=ib::status;if(!quickSettings_)return statusBarRect_.contains(x,y)?openQuickSettings():false;if(quickSettingsWait_){quickSettings_=quickSettingsWait_=false;return true;}const int id=statusTargets_.find(x,y);if(id==S::Open||id==S::Close||!S::Panel.contains(x,y)){quickSettings_=false;damage(S::Panel);refreshStatus(true);++presentedVersion_;return true;}if(id==S::Wifi){quickSettings_=false;wifiView_.open();wifiPaint();startWifi(-1,{});++presentedVersion_;return true;}if(id==S::LightOff||(id>=S::LevelBase&&id<S::LevelBase+5)){const auto state=light_.view();const int level=S::nativeLevel(id==S::LightOff?0:id-S::LevelBase+1,state.maximum);if(level>=0){if(!state.busy)(void)light_.request(app_,level);else if(!state.changing)pendingLight_=level; /* finishes after the background read */}refreshStatus(true);return true;}return true;}
bool Shell::gesture(const reader::Touch::Tap& t){if(lock_!=Lock::None||timezoneView_.active())return false;using G=reader::Gesture;if(t.gesture==G::EdgeDown){if(t.startY>80)return false;if(t.startX>=1080)return openQuickSettings();return t.startX>=540?goHome():false;}if(t.gesture==G::VerticalScroll){if(t.mode!=reader::DragMode::Text||t.epoch!=version()||touchMode()!=reader::DragMode::Text||t.startY<=80)return false;return twoFingerPan(t.deltaX,t.deltaY,t.originY);}if(t.gesture==G::Pinch)return pinch(t.scale,t.focalX,t.focalY);if(t.gesture==G::TwoFingerPan)return twoFingerPan(t.deltaX,t.deltaY,t.originY);return gesture(t.gesture);}
double Shell::pdfZoom(){if(document_&&selectedFormat_=="epub")return epubZoom_.scale();if(!document_||selectedFormat_!="pdf"||currentViewSet_.isEmpty())return 1;const QString key=currentViewSet_+QString(":%1").arg(page());if(key==zoomKey_)return zoom_;zoomKey_=key;zoom_=1;
 try{auto receipt=reader::viewstore::readSet(selectedData_,currentViewSet_);if(auto entry=reader::viewstore::pageEntry(receipt,page()))zoom_=entry->view.zoom;}catch(...){zoom_=1;}return zoom_;}
bool Shell::pinch(float scale,int fx,int fy){
 if(stop_||lock_!=Lock::None||timezoneView_.active()||quickSettings_||wifiView_.active()||companionView_.active()||notebook_.active()||(document_&&document_->contact()))return false;
 if(tom_&&tom_->active())return !stop_&&tom_->pinch(scale,fx,fy);
 if(stop_||!document_||(tom_&&tom_->active())||(notes_&&notes_->active())||viewControls_.active()||toolsView_.active()||menu_||(state_!=State::Clean&&state_!=State::Chrome))return false;
 if(selectedFormat_=="epub"){if(document_->contact()||!std::isfinite(scale)||scale<=0)return false;if(zoomPixels_.empty())zoomPixels_.resize(underlay::Pixels);if(epubZoom_.zoom(scale,fx,fy)){++presentedVersion_;semanticPaint();tomDamage({0,0,1620,2060});}return true;}
 auto s=viewSnapshot();auto view=s.pdf;const double z=std::max(1.0,view.zoom);const double next=std::clamp(std::round(z*double(scale)*20.)/20.,1.,4.);if(std::abs(next-z)<0.02)return true;
 if(next<=1.001){view.zoom=1;view.pan={};}
 else{const double cx=reader::pdfview::Width/2.,cy=reader::pdfview::Height/2.;const double px=(fx-cx)-(fx-cx-view.pan.x)*next/z,py=(fy-cy)-(fy-cy-view.pan.y)*next/z;
  view.zoom=next;view.pan={};try{auto p=reader::pdfview::plan(s.sourceBounds,view);view.pan={std::clamp(px,-p.maxPanX,p.maxPanX),std::clamp(py,-p.maxPanY,p.maxPanY)};}catch(...){return false;}}
 if(!applyPdfView(view,true))notice("Zoom is unavailable on this page right now");return true;}
bool Shell::twoFingerPan(int dx,int dy,int originY){
 if(stop_||lock_!=Lock::None||timezoneView_.active()||quickSettings_||wifiView_.active()||companionView_.active()||notebook_.active()||(document_&&document_->contact()))return false;
 if(tom_&&tom_->active())return !stop_&&tom_->viewportPan(dx,dy);
 if(epubZoomActive()&&!stop_&&!document_->contact()&&!toolsView_.active()&&!viewControls_.active()&&!menu_){if(epubZoom_.pan(dx,dy)){++presentedVersion_;semanticPaint();tomDamage({0,0,1620,2060});}return true;}
 if(lock_!=Lock::None||timezoneView_.active()||quickSettings_||wifiView_.active()||libraryManage_.active()||companionView_.active()||notebook_.active())return false;if(!document_)return !stop_&&state_==State::Picker&&originY<=160&&dy>=250&&std::abs(dy)>=2*std::abs(dx)?openRecents():false;
 if(stop_||!document_||(tom_&&tom_->active())||(notes_&&notes_->active())||viewControls_.active()||toolsView_.active()||(state_!=State::Clean&&state_!=State::Chrome))return false;
 if(originY<=160&&dy>=250&&std::abs(dy)>=2*std::abs(dx))return openRecents(); /* two fingers down from the top: the recent drawer */
 if(menu_||selectedFormat_!="pdf")return false;auto s=viewSnapshot();auto view=s.pdf;if(view.zoom<=1.001)return false;
 try{auto at=view;at.pan={};auto p=reader::pdfview::plan(s.sourceBounds,at);const reader::pdfview::Point next{std::clamp(view.pan.x+dx,-p.maxPanX,p.maxPanX),std::clamp(view.pan.y+dy,-p.maxPanY,p.maxPanY)};
  if(std::abs(next.x-view.pan.x)<1&&std::abs(next.y-view.pan.y)<1)return true;view.pan=next;}catch(...){return false;}
 if(!applyPdfView(view,true))notice("Panning is unavailable on this page right now");return true;}
bool Shell::resetZoom(){if(document_&&selectedFormat_=="epub"){epubZoom_.reset();++presentedVersion_;semanticPaint();tomDamage({0,0,1620,2060});return true;}if(!document_||selectedFormat_!="pdf")return false;auto view=viewSnapshot().pdf;if(view.zoom<=1.001)return true;view.zoom=1;view.pan={};if(!applyPdfView(view,true))notice("The page is busy — try again in a moment");return true;}
bool Shell::gesture(reader::Gesture g){if(timezoneView_.active()||lock_!=Lock::None)return false;
 using G=reader::Gesture;if(stop_||g==G::None)return false;recovery::v2::e2::mark("reader:gesture",unsigned(g));
 const bool overlay=quickSettings_||wifiView_.active()||viewControls_.active()||toolsView_.active()||(tom_&&tom_->active())||(notes_&&notes_->active());
 if(overlay)return false; /* panels keep their own controls; a gesture never acts behind them */
 if(g==G::Undo||g==G::Redo){const bool ok=styled_&&writable()&&(g==G::Undo?styled_->undo():styled_->redo());if(ok)expectRemoval();return ok;}
 if(semantics_->view()!=semantic::View::None){semantics_->dismiss();if(semantics_->takeDirty())semanticPaint();}
 if((g==G::SwipeNext||g==G::SwipePrevious)&&document_&&(state_==State::Clean||state_==State::Chrome||state_==State::NavigationPending))return action(g==G::SwipeNext?Action::Next:Action::Previous);
 if(g==G::EdgeDown)return goHome();
 if(g==G::EdgeUp&&document_&&(state_==State::Clean||state_==State::Chrome))return openQuickBrowse(); /* quick browse from the bottom edge */
 if((g==G::SwipeNext||g==G::SwipePrevious)&&state_==State::Picker)return libraryPage(g==G::SwipeNext?1:-1);
 return false;
}
bool Shell::action(Action a,bool markerContact){if(timezoneView_.active()||stop_||lock_!=Lock::None)return false;if(a==Action::None)return false;if(viewControls_.active()){++rejected_;return false;}if(tom_&&tom_->active()){++rejected_;return false;}if(notes_&&notes_->active()){++rejected_;return false;}if(toolsView_.active()){++rejected_;return false;}if(markerContact||(document_&&document_->contact())){++rejected_;return false;}if(a==Action::Exit&&styled_&&styled_->status().busy){clearNavigation();pendingAction_=a;return true;}
 if(a==Action::Exit){clearNavigation();if(document_&&!document_->request(reader::Action::Exit)){++rejected_;return false;}recovery::v2::e2::mark("reader:exit-action",unsigned(state_));state_=State::ExitPending;stop_=true;return true;}
 if(a==Action::Open)return choose(libraryOffset_);
 if(a==Action::Show&&state_==State::Clean){selectionChrome_=true;state_=State::Chrome;menu_=false;if(chromeStale_)paintChrome(true);change_.regions|=ChromeRegions;return true;}
 if(a==Action::Hide&&state_==State::Chrome){selectionChrome_=false;state_=State::Clean;if(menu_){menu_=false;pageBackgrounds();damage(menuRect_);}change_.regions|=ChromeRegions;return true;}
 if((a==Action::Previous||a==Action::Next)&&(state_==State::Chrome||state_==State::Clean||state_==State::NavigationPending)&&document_){int base=navigationIntent_>=0?navigationIntent_:page();int target=std::clamp(base+(a==Action::Next?1:-1),0,document_->total()-1);if(target==base){++rejected_;return false;}return navigateTo(target);}
 ++rejected_;return false;
}
bool Shell::startCompanion(QString action,QString pair,QString account){if((!companionEnabled_&&action!="inbox")||companionBusy_||companionStop_)return false;if(companionThread_.joinable())companionThread_.join();companionDone_=false;companionBusy_=true;inboxRunning_=action=="inbox";const auto prior=companionStatus_;auto shown=companionStatus_;shown["busy"]=true;if(action!="inbox"&&companionView_.active())companionView_.update(shown);
 companionThread_=std::thread([this,action,pair,account,prior]{try{if(action=="inbox"){auto catalogue=[&]{try{return underlay::digest(underlay::readBounded(data_/"catalogue.json",2*1024*1024));}catch(...){return QByteArray{};}};auto before=catalogue();auto errors=[&]{try{return underlay::readBounded(data_/"import-errors.json",16384);}catch(...){return QByteArray{};}};const auto oldErrors=errors();reader::syncLibrary(app_,data_,companionStop_);companionResult_=prior;companionResult_["library_changed"]=before!=catalogue();const auto nextErrors=errors();companionResult_["inbox_errors_changed"]=QJsonDocument::fromJson(oldErrors).array()!=QJsonDocument::fromJson(nextErrors).array();companionResult_["inbox_error_count"]=QJsonDocument::fromJson(nextErrors).array().size();companionResult_["operation_failed"]=false;}else companionResult_=reader::companionCommand(app_,data_,action,pair,account,companionStop_);}catch(...){companionResult_=prior;companionResult_["operation_failed"]=true;companionResult_["error"]="unavailable";}companionDone_.store(true,std::memory_order_release);});return true;}
void Shell::startWifi(int selected,QString password){
 std::optional<wifi::Network> target;if(selected>=0&&selected<int(wifiView_.networks().size()))target=wifiView_.networks()[std::size_t(selected)];
 runWifi(std::move(target),std::move(password),selected==-2);
}
void Shell::runWifi(std::optional<wifi::Network> target,QString password,bool automatic){
 if(wifiStop_)return;
 const auto decision=wifiRequests_.submit({target,password,automatic});if(decision==wifi::Requests::Decision::Refused)return;
 if(!automatic){wifi::Status found;{std::lock_guard lock(wifiMutex_);found=wifiFoundStatus_;} /* the scan worker assigns it under the lock */wifiView_.update(wifiView_.networks(),found,target?"CONNECTING TO "+target->ssid.left(30):QString("SCANNING"),true);}
 if(decision==wifi::Requests::Decision::Queued)return;
 if(wifiThread_.joinable())wifiThread_.join();
 wifiBusy_=true;wifiDone_=false;
 wifiThread_=std::thread([this,target,password,automatic]{
  wifi::Control control;QString result;
  try{
   if(target)result=control.connect(*target,password,25000,&wifiStop_);
   else{const auto status=control.status(&wifiStop_);if(!automatic||(!status.connected()&&status.state!="INTERFACE_DISABLED")){control.scan(&wifiStop_);for(int n=0;n<30&&!wifiStop_.load();++n)std::this_thread::sleep_for(std::chrono::milliseconds(100));if(automatic&&!wifiStop_)control.reconnect(control.status(&wifiStop_),&wifiStop_);}}
   auto found=control.networks(&wifiStop_);auto status=control.status(&wifiStop_);
   std::lock_guard lock(wifiMutex_);wifiFound_=std::move(found);wifiFoundStatus_=status;wifiResult_=target?(result.isEmpty()?QString("CONNECTED"):result):QString();
  }catch(...){std::lock_guard lock(wifiMutex_);wifiResult_="WI-FI ERROR - TRY AGAIN";}
  wifiDone_.store(true,std::memory_order_release);});
}
void Shell::wifiTick(){const auto now=std::chrono::steady_clock::now();if(wifiBusy_&&wifiDone_.load(std::memory_order_acquire)){wifiThread_.join();wifiBusy_=false;bool changed=false;{std::lock_guard lock(wifiMutex_);changed=wifiView_.update(wifiFound_,wifiFoundStatus_,wifiResult_,false);}nextWifi_=now+std::chrono::seconds(wifiFoundStatus_.connected()?30:15);if(auto next=wifiRequests_.finish()){runWifi(std::move(next->target),std::move(next->password),false);changed=true;}if(changed&&wifiView_.active()&&(state_==State::Picker||document_))wifiPaint();}
 if(libraryMode_&&releaseNetworking_&&now>=nextWifi_&&!stop_&&!powerConstrained_&&!wifiBusy_&&!wifiRequests_.queued()&&!wifiView_.active()&&!wifiPassword_.active()){nextWifi_=now+std::chrono::seconds(30);startWifi(-2,{});}}
void Shell::wifiPaint(){if(!wifiView_.active())return;auto& pixels=document_?viewPixels_:chrome_;auto& before=libraryBefore_;before.assign(pixels.begin(),pixels.end()); /* reused frame */spike::Surface s{pixels,1620,2160,1632};wifiView_.paint(s);if(wifiPassword_.active())wifiPassword_.paint(s);std::array<spike::Rect,8> changed;const auto count=changedRegions(before,pixels,1620,2160,1632,0,changed);for(unsigned i=0;i<count;++i)damage(changed[i]);}
void Shell::companionTick(){if(!libraryMode_)return;
 if(companionBusy_&&companionDone_.load(std::memory_order_acquire)){companionThread_.join();companionBusy_=false;auto before=companionStatus_;companionStatus_=std::move(companionResult_);auto state=companionStatus_["state"].toString();qint64 delay=state=="transferring"||(state=="paired"&&companionStatus_.value("pending_jobs").toInt()>0)?500:state=="pairing"?2000:30000;auto retry=companionStatus_["next_poll"].toDouble()-double(QDateTime::currentSecsSinceEpoch());if(std::isfinite(retry)&&retry>0)delay=std::max<qint64>(delay,qint64(std::min(retry,86400.0)*1000));if(companionView_.active()){auto shown=companionStatus_;shown["busy"]=false;companionView_.update(shown);}if(inboxRunning_&&!companionStatus_["operation_failed"].toBool()){inboxPassed_=inboxStarted_;inboxPassedValid_=true;}if(!inboxRunning_)nextCompanion_=std::chrono::steady_clock::now()+std::chrono::milliseconds(delay);inboxRunning_=false;
  if(companionStatus_["library_changed"].toBool())reloadLibrary();if(companionStatus_["inbox_errors_changed"].toBool()&&companionStatus_["inbox_error_count"].toInt()>0){if(document_)notice("Some Inbox files were rejected — your originals are kept");else setLibraryNotice("Some files were rejected — your originals are kept");}else if(companionStatus_["inbox_errors_changed"].toBool()&&companionStatus_["inbox_error_count"].toInt()==0&&libraryView_.notice()==QString("Some files were rejected — your originals are kept"))setLibraryNotice({});
  bool visibleChanged=false;for(auto key:{"state","pairing_id","pairing_code","account","expires_at","received","total","error","retry_available","operation_failed"})visibleChanged|=before[key]!=companionStatus_[key];
  if(state_==State::Picker&&!libraryManage_.active()){if(companionView_.active()&&visibleChanged){bool same=before["state"]==companionStatus_["state"]&&before["pairing_id"]==companionStatus_["pairing_id"]&&before["account"]==companionStatus_["account"];companionView_.update(companionStatus_);companionView_.paint({chrome_,1620,2160,1632});if(same){damage({60,1240,1560,1560});damage({60,1600,1560,1840});}else change_.full=true;}else if(!librarySheetOpen()&&(companionStatus_["library_changed"].toBool()||companionStatus_["inbox_errors_changed"].toBool())){libraryPaint();change_.full=true;}}
 }
 if(companionEnabled_&&!stop_&&!powerConstrained_&&!companionBusy_&&!libraryManage_.active()&&std::chrono::steady_clock::now()>=nextCompanion_){auto state=companionStatus_["state"].toString();QString action;if(state=="paired"||state=="transferring"||state=="transfer-error")action="sync";else if(state=="pairing")action=companionStatus_["expires_at"].toDouble()>QDateTime::currentSecsSinceEpoch()?"poll":"status";
  if(!action.isEmpty())(void)startCompanion(action);
 }
 if(!stop_&&!powerConstrained_&&!companionBusy_&&!opener_.joinable()&&!libraryManage_.active()&&std::chrono::steady_clock::now()>=nextInbox_){nextInbox_=std::chrono::steady_clock::now()+std::chrono::seconds(5);/* An unchanged listing needs no helper, and a listing still changing (a file arriving) waits until two looks 5 s apart agree. */if(auto signature=inboxSignature()){const bool settled=inboxSeenValid_&&*signature==inboxSeen_;inboxSeen_=*signature;inboxSeenValid_=true;if(settled&&!(inboxPassedValid_&&*signature==inboxPassed_)&&startCompanion("inbox"))inboxStarted_=*signature;}}
}
std::optional<std::string> Shell::inboxSignature()const{
 std::error_code ec;const auto inbox=data_/"inbox";if(!std::filesystem::exists(inbox,ec))return std::string{};
 std::vector<std::string> rows;
 for(const auto& e:std::filesystem::directory_iterator(inbox,ec)){if(e.path().filename()=="imported")continue;if(rows.size()>=64)break;
  struct stat st{};if(lstat(e.path().c_str(),&st))return std::nullopt;
  rows.push_back(e.path().filename().string()+'\0'+std::to_string(st.st_ino)+':'+std::to_string(st.st_size)+':'+std::to_string(st.st_mtim.tv_sec)+'.'+std::to_string(st.st_mtim.tv_nsec)+':'+std::to_string(st.st_ctim.tv_sec)+'.'+std::to_string(st.st_ctim.tv_nsec));}
 if(ec)return std::nullopt;std::sort(rows.begin(),rows.end());std::string out;for(const auto& row:rows)out+=row+'\n';return out;
}
bool Shell::tick(){if((tom_&&tom_->contact())||(document_&&document_->contact()))return false;if(exiting())return false;{const auto before=light_.view();if(light_.tick()){const auto after=light_.view();const bool visible=before.available!=after.available||before.requested!=after.requested||before.power!=after.power||before.maximum!=after.maximum||before.error!=after.error||before.changing!=after.changing;refreshStatus(false);if(pendingLight_>=0){const int level=pendingLight_;pendingLight_=-1;if(light_.request(app_,level))refreshStatus(true);}if(visible&&menu_&&menuAnchor_.x0==LightAnchor.x0){auto previous=menuRect_;paintChrome(true);damage(previous);damage(menuRect_);}}}if(quickSettingsWait_&&(!light_.view().busy||std::chrono::steady_clock::now()>=quickSettingsUntil_)){quickSettingsWait_=false;refreshStatus(true);damage(ib::status::Panel);++presentedVersion_;}if(statusTapPending_){statusTapPending_=false;(void)finger(statusLast_.x,statusLast_.y);}if(notes_&&notes_->failed()&&notes_->unsaved()==0)(void)retireFailedNotes();if(tom_&&tom_->failed()&&tom_->unsaved()==0)(void)retireFailedTom();
 if(notesReload_&&!notes_&&styled_&&!styled_->status().busy&&styled_->reload()){notesReload_=false;expectRemoval();}pollDevice();pollClock(QDateTime::currentSecsSinceEpoch());refreshStatus();coverTick();lockTick();
 (void)dispatchNavigation();
 if(exitAfterNavigation_&&(!document_||returning_||(tom_&&tom_->active())||(notes_&&notes_->active())))exitAfterNavigation_=false;
 if(exitAfterNavigation_&&(state_==State::Clean||state_==State::Chrome)&&returnToLibrary())exitAfterNavigation_=false;
 if(pendingAction_!=Action::None&&inkSettled()){auto queued=pendingAction_;pendingAction_=Action::None;action(queued);}
 revealSceneChanged();revealTick();companionTick();wifiTick();
 if(changingView_){viewTick();return false;}
 // Keep an open settings sheet in step with the reader (busy/contact flags enable its controls).
 // The two flags are compared from memory first; the full snapshot (view-set files, bookmarks, ink pins) is only
 // built when one of them changed, not on every tick while the sheet is open.
 if(viewControls_.active()&&document_&&!document_->contact()){const auto& applied=viewControls_.applied();const bool busy=changingView_||(styled_&&styled_->status().busy)||semantics_->pending(),quiet=styled_&&styled_->revealEnabled();
  if((busy!=applied.busy||quiet!=applied.quietEffects)&&viewControls_.update(viewSnapshot()))viewPaint();}
 thumbnailTick();snapTick();selectTick();
 if(tom_&&tom_->active()){tomTick();return false;}
 sourceTick();
 if(notes_&&notes_->active()){notesTick();return false;}
 if(syncing_){if(!done_)return false;opener_.join();if(!finishOpener()){fail(10);return false;}syncing_=false;state_=State::Picker;reloadLibrary();
  if(creatingNotebook_){creatingNotebook_=false;notebook_.close();int created=-1;for(int i=0;i<books_.size();++i)if(!books_[i].trashed&&!idsBeforeImport_.contains(books_[i].id))created=i;
   if(openFailed_||created<0){pendingTomNote_={};setLibraryNotice("The notebook could not be created");libraryPaint();change_.full=true;return true;}
   if(!notebookFolder_.isEmpty())try{library::Store store(data_);store.organize(books_[created].id,notebookFolder_);reloadLibrary();}catch(...){}
   for(int i=0;i<books_.size();++i)if(!idsBeforeImport_.contains(books_[i].id)&&!books_[i].trashed){if(pendingTomNote_["waitNotebook"].toBool()){pendingTomNote_["waitNotebook"]=false;pendingTomNote_["destination"]=books_[i].id;}setLibraryNotice({});libraryPaint();return choose(unsigned(i));}
   libraryPaint();change_.full=true;return true;}
  setLibraryNotice(openFailed_?"Import failed — your source files are kept":"");libraryPaint();change_.full=true;return true;}
 if(returning_){if(notes_){if(!notes_->finish(10))return false;notes_.reset();noteIndicators_.clear();}if(styled_){if(!styled_->finish(10))return false;retiredStyledCommits_+=styled_->status().saved;styled_.reset();styledPage_.clear();}if(!document_->finish(10)||!semantics_->finish(10))return false;presentedInk_={};std::fill(clean_.begin(),clean_.end(),0xffffffffu);document_.reset();openingLease_.reset();
  /* Nothing of the closed document may be composited over My Files: its panels, menus, selection tool, pending pen
     selection or snap, page thumbnails and reader notice all go with it. */
  pdfPreviewActive_=false;quickSettings_=quickSettingsWait_=false;toolsView_={};viewControls_={};menu_=false;selectMode_=false;penSelect_={};penRangeCount_=0;penRangeOverflow_=false;snapStroke_={};cleanupNext_=false;thumbnails_.reset();notice_.clear();pageFirstPending_=false;recovery::v2::e2::mark("ui:document-closed");semantics_=std::make_unique<semantic::Controller>(app_,data_);semanticVisual_={};std::fill(semanticOverlay_.begin(),semanticOverlay_.end(),0);noteIndicators_.clear();exitAfterNavigation_=false;
  /* Notes written, starred or deleted in the book must show in Library > Notes straight away. */
  reloadLibrary();if(libraryView_.section()==LibraryView::Section::Notes)libraryNotes();libraryPaint();returning_=false;state_=State::Picker;change_.complete(FullRefreshReason::DocumentBinding);++presentedVersion_;return true;}
 if(styled_&&styled_->status().failed){fail(11);return false;}if(styled_&&(!document_||styledPage_==document_->active().prepared.binding)){if(styled_->tick()){auto visible=[](ib::reader::Tools t){t.canUndo=t.canUndo&&!t.busy;t.canRedo=t.canRedo&&!t.busy;t.busy=false;return t;};auto r=styled_->damage();const bool toolsChanged=visible(railModel())!=visible(paintedTools_);const bool repaint=pageFirstPending_||!r.empty()||!toolsPainted_||toolsChanged||chromeModel().status!=paintedStyledStatus_;
   /* A stroke result changes only the ink layer and the chrome's state (footer status, Undo/Redo): repaint the chrome,
      not the page background (a 14 MB copy, highlight wash, page snapshot hash and note markers). The page's first
      result still rebuilds everything. When Undo/Redo availability changes with the toolbar shown, its column is
      damaged too; before, only the footer was, which left a stale tile on screen. */
   if(repaint){if(pageFirstPending_)pageBackgrounds();else paintChrome();damage({0,2060,1620,2160});if(toolsChanged&&state_==State::Chrome&&!chromeStale_)damage(ib::reader::toolbarRect());}++toolsRevision_;
   /* page-first: the bootstrap result of a page just shown is never a flash, whatever its size (ink appears over the page) */
   const bool first=pageFirstPending_;if(first){pageFirstPending_=false;recovery::v2::e2::mark("reader:page-first-ink-us",lapUs(pageFirstAt_));}
   if(!r.empty()&&cleanupNext_&&!first){if(change_.cleanup.empty())change_.cleanup=contentRect(r);else change_.cleanup.include(contentRect(r));recovery::v2::e2::mark("ui:cleanup-requested",unsigned(std::uint64_t(r.x1-r.x0)*(r.y1-r.y0)/1000));}if(!first)cleanupNext_=false;
   if(!r.empty()){if(viewportRefresh_)tomDamage(contentRect(r));else damage(contentRect(r));}if(first&&viewportRefresh_)viewportRefresh_=false;if(toolsView_.active()){toolsView_.update(toolsSnapshot());if(repaint)toolsPaint();}}}
 if(styled_&&styled_->status().ready&&!styled_->status().contact)syncToolShortcuts();
 semantics_->tick();
 if(!document_&&state_==State::Loading&&done_.load(std::memory_order_acquire)){opener_.join();if(!finishOpener()){fail(12);return false;}if(openFailed_||!prepared_){pdfPreviewActive_=false;if(libraryMode_){tomSource_={};pendingNativeOutput_={};pendingOpen_.clear();openingLease_.reset();prepared_.reset();state_=State::Picker;pendingNote_={};setLibraryNotice("This book could not open \u2014 its data is kept");libraryPaint();change_.full=true;return true;}fail(13);return false;}currentViewSet_=openingViewSet_;recovery::v2::e2::mark("reader:open-joined");document_=std::make_unique<reader::Reader>(app_,selectedData_,std::move(*prepared_),currentViewSet_,preparationTracking_,resumeTracking_);recovery::v2::e2::mark("reader:open-document");if(libraryMode_)setLibraryNotice({}); /* "Opening …" ends with the open; left set, it showed on My Files after close */prepared_.reset();semantics_->bind(document_->active().prepared);awaitingSemanticPage_=true;}
 // D2: show the page without waiting for its highlight/selection data (it loads on a low-priority
 // worker and used to delay every open and page turn); it is painted when it arrives.
 if(awaitingSemanticPage_){auto lap=std::chrono::steady_clock::now();semantics_->tick();const auto semanticUs=lapUs(lap);semanticLate_=!semantics_->ready();if(libraryMode_){ /* page-first: install once this page's ink layer exists (the old one has closed, so its ink can never show here);
   its bootstrap finishes in the background, the pen stays gated by writable() until then, and its first result is a partial update */
   const bool ready=bindStyled();if(!styled_||styledPage_!=document_->active().prepared.binding)return false;
   pageFirstPending_=!ready;pageFirstAt_=std::chrono::steady_clock::now();recovery::v2::e2::mark("reader:page-first",ready?1u:0u);}
  recovery::v2::e2::mark("ui:open-semantic-tick-us",semanticUs);recovery::v2::e2::mark("ui:open-bind-styled-us",lapUs(lap));awaitingSemanticPage_=false;pdfPreviewActive_=false;pageBackgrounds();recovery::v2::e2::mark("ui:open-page-backgrounds-us",lapUs(lap));if(!noteSource_.isEmpty()){auto current=std::find_if(books_.begin(),books_.end(),[&](const auto& b){return b.id==selectedId_;});bool resolved=current!=books_.end()&&notes::resolveAnnotation(noteSource_,product::pageSnapshot(*current,document_->active().prepared))["status"]=="resolved";if(!resolved)notice_="The note\u2019s source changed \u2014 the note is kept";noteSource_={};}if(viewRefused_){viewRefused_=false;notice_="That text size can\u2019t be used for this book yet \u2014 it stays as it was";}presentedInk_=document_->active().prepared.ink;state_=selectMode_&&selectionChrome_?State::Chrome:State::Clean;if(quickBrowsing()){state_=State::Chrome;paintChrome(true);} /* quick browse stays open on the new page */recovery::v2::e2::mark("reader:open-ready");if(navigationActiveOperation_&&page()==navigationSubmitted_)recovery::v2::e2::mark("ui:nav-frame-ready",(navigationActiveOperation_<<10)|unsigned(page()));lap=std::chrono::steady_clock::now();semanticVisual_=semantic::transient({semanticOverlay_,1620,2160,1632},*semantics_,background(),{libraryMode_,bool(styled_),state_==State::Chrome?ib::reader::toolbarRect():spike::Rect{}});recovery::v2::e2::mark("ui:open-transient-us",lapUs(lap));semantics_->takeDirty();if(viewportRefresh_){change_={};tomDamage({0,0,1620,2060});}else if(navigationActiveOperation_&&page()==navigationSubmitted_){change_={};change_.pageTurn=true;tomDamage({0,0,1620,2160});}else{change_={};change_.complete(FullRefreshReason::DocumentBinding);}++presentedVersion_;return true;}
 if(document_){auto navLap=std::chrono::steady_clock::now();bool changed=document_->tick();if(changed)recovery::v2::e2::mark("reader:doc-tick-us",lapUs(navLap));if(document_->failed()||document_->exiting()){recovery::v2::e2::mark("reader:document-ended",document_->failed()?1u:2u);fail(14);return false;}
  auto refusal=document_->takeNavigationRefusal();if(refusal!=reader::NavigationRefusal::None){if(navigationActiveOperation_)recovery::v2::e2::mark("ui:nav-failed",(navigationActiveOperation_<<10)|unsigned(std::max(0,navigationSubmitted_)));const int refused=navigationSubmitted_;navigationSubmitted_=-1;navigationActiveOperation_=0;
   if(refusal==reader::NavigationRefusal::Busy&&navigationIntent_>=0){navigationRetryAt_=std::chrono::steady_clock::now()+std::chrono::milliseconds(25);state_=State::NavigationPending;return false;}
   if(navigationIntent_>=0&&navigationIntent_!=refused){state_=State::NavigationPending;return false;}navigationIntent_=-1;navigationOperation_=0;tomSource_={};noteSource_={};state_=selectMode_&&selectionChrome_?State::Chrome:State::Clean;semantics_->notify(refusal==reader::NavigationRefusal::Busy?"The page is still saving \u2014 try again in a moment":"That page isn\u2019t available \u2014 your data is kept");semanticPaint();change_.regions|=ChromeRegions;return false;}
  if(changed){if(navigationActiveOperation_&&page()==navigationSubmitted_)recovery::v2::e2::mark("ui:nav-content-ready",(navigationActiveOperation_<<10)|unsigned(page()));semantics_->bind(document_->active().prepared);recovery::v2::e2::mark("ui:semantic-bind-us",lapUs(navLap));awaitingSemanticPage_=true;return tick();}
  if(state_==State::NavigationPending&&navigationIntent_<0&&document_->state()==reader::State::Active){state_=selectMode_&&selectionChrome_?State::Chrome:State::Clean;pageBackgrounds();change_.regions|=ChromeRegions;}
  else if(state_==State::NavigationPending&&!loadingShown_){loadingChrome_=true;paintChrome();change_.regions|=4;loadingShown_=true; /* Loading shows in the footer */}
  if(semanticLate_&&semantics_->ready()&&(state_==State::Clean||state_==State::Chrome)){semanticLate_=false;semantics_->takeDirty();semanticPaint();}
  if(state_!=State::NavigationPending&&semantics_->takeDirty())semanticPaint(); /* D2: data that arrives mid page-turn waits for the new page */
 }
 // Tom answer saved from another page: once that page is shown, create its note there.
 if(!pendingTomNote_.isEmpty()&&!pendingTomNote_["waitNotebook"].toBool()&&document_&&styled_&&styled_->status().ready&&!styled_->status().busy&&state_==State::Clean&&!awaitingSemanticPage_){auto pending=pendingTomNote_;pendingTomNote_={};bool valid=pending["destination"]==selectedId_;auto source=pending["source"].toObject();if(!source.isEmpty())valid=valid&&source["document"]==selectedId_&&source["page"].toInt(-1)==page();if(!valid)notice("The destination changed — Tom's reply is kept in History");else if(openNotes(notes::Mode::Note)){if(pending["ink"].toObject().isEmpty())notes_->queueText(pending["title"].toString(),pending["text"].toString());else notes_->queueInk(pending["title"].toString(),pending["ink"].toObject());}}
 // Library note opened: go to the note's source page, then show the note there.
 if(!pendingNote_.document.isEmpty()&&document_&&styled_&&state_==State::Clean&&!awaitingSemanticPage_){auto pending=pendingNote_;
  auto book=std::find_if(books_.begin(),books_.end(),[&](const auto& b){return b.id==selectedId_;});
  if(pending.document!=selectedId_||book==books_.end()){pendingNote_={};}
  else{int target=notes::sourcePage(pending.anchor,product::pageSnapshot(*book,document_->active().prepared));
   if(target>=0&&target!=page()){if(!navigateTo(target)){pendingNote_={};notice("The note’s source page is unavailable — the note is kept");}}
   else{pendingNote_={};if(target<0)notice_="The note’s source changed — the note is kept";openNotes(notes::Mode::Note,{},pending.note);}}}
 return false;
}
void Shell::installed(){recovery::v2::e2::mark("reader:nav-installed",unsigned(std::max(0,page())));presentationAccepted();if(document_){document_->installed();if(document_->state()==reader::State::Ready)document_->activate();}
 if(navigationActiveOperation_&&page()==navigationSubmitted_){recovery::v2::e2::mark("ui:nav-software-submit",(navigationActiveOperation_<<10)|unsigned(page()));if(navigationIntent_==page()){if(navigationOperation_!=navigationActiveOperation_)recovery::v2::e2::mark("ui:nav-satisfied",(navigationOperation_<<10)|unsigned(page()));navigationIntent_=-1;navigationOperation_=0;}navigationSubmitted_=-1;navigationActiveOperation_=0;}}
// ---- Settings > Tom, Accessibility (ui-prefs.json) -------------------------------------------------------------------
void Shell::applyPrefs(){ib::theme::setHighContrast(prefs_.highContrast);ib::text::setPlainScript(prefs_.plainText);reducedMotion_=prefs_.reduceMotion;if(prefs_.reduceMotion)libraryEffects_=false;}
void Shell::savePrefs(){try{prefs_.save(data_);}catch(...){setLibraryNotice("Setting not saved");}}
// ---- Settings > Security: the passcode screen ----------------------------------------------------------------------
void Shell::beginLock(Lock mode){lock_=mode;lockEntry_.clear();if(mode==Lock::Choose||mode==Lock::Unlock)lockFirst_.clear();if(mode!=Lock::Confirm)lockMessage_.clear();toolsView_={};menu_=false;lockPaint(true);}
void Shell::lockPaint(bool full){
 if(lockPixels_.size()<underlay::Pixels)lockPixels_.assign(underlay::Pixels,0xffffffffu);
 ib::lock::Model m;m.prompt=lock_==Lock::Unlock?"Enter passcode":lock_==Lock::Verify?"Enter your current passcode":lock_==Lock::Choose?"Choose a 4-digit passcode":"Enter it again";
 m.entered=int(lockEntry_.size());m.length=security::PasscodeLength;m.message=semantic::utf8(lockMessage_);m.cancel=lock_!=Lock::Unlock;m.waiting=std::chrono::steady_clock::now()<lockWaitUntil_;
 ib::Canvas c({lockPixels_,underlay::Width,underlay::Height,underlay::Stride});lockTargets_.clear();ib::lock::paint(c,lockTargets_,m);
 if(full){change_.complete(FullRefreshReason::Security);return;}
 damage({ib::lock::DotsRect.x0,ib::lock::DotsRect.y0,ib::lock::DotsRect.x1,ib::lock::MessageRect.y1});damage({ib::lock::MessageRect.x0,ib::lock::MessageRect.y0,ib::lock::MessageRect.x1,ib::lock::MessageRect.y1});
 damage({ib::lock::KeypadRect.x0,ib::lock::KeypadRect.y1-260,ib::lock::KeypadRect.x1,ib::lock::KeypadRect.y1});}
void Shell::finishLock(){lock_=Lock::None;lockEntry_.clear();lockFirst_.clear();lockMessage_.clear();lockThenChoose_=false;lockPixels_.clear();lockPixels_.shrink_to_fit();lockTargets_.clear();
 if(libraryMode_&&!document_)libraryPaint();change_.complete(FullRefreshReason::Security);}
bool Shell::lockTap(int x,int y){
 namespace L=ib::lock;const int id=lockTargets_.find(x,y);if(std::chrono::steady_clock::now()<lockWaitUntil_)return true;
 if(id==L::Cancel&&lock_!=Lock::Unlock){finishLock();return true;}
 if(id==L::Back){if(!lockEntry_.isEmpty())lockEntry_.chop(1);lockPaint(false);return true;}
 if(id<L::KeyBase||id>L::KeyBase+9)return true; /* the rest of the screen does nothing while locked */
 lockEntry_+=QChar(char16_t(u'0'+(id-L::KeyBase)));lockMessage_.clear();if(lockEntry_.size()<security::PasscodeLength){lockPaint(false);return true;}
 const QString code=lockEntry_;lockEntry_.clear();
 if(lock_==Lock::Choose){lockFirst_=code;lock_=Lock::Confirm;lockPaint(true);return true;}
 if(lock_==Lock::Confirm){if(code!=lockFirst_){lockFirst_.clear();lock_=Lock::Choose;lockMessage_="The passcodes didn\u2019t match \u2014 try again";lockPaint(true);return true;}
  try{security::save(data_,code);finishLock();notice("Passcode on");}catch(...){finishLock();notice("Passcode not saved");}return true;}
 const auto stored=security::load(data_);const bool ok=!stored||security::matches(*stored,code); /* a passcode removed from outside no longer locks */
 if(!ok){if(++lockFailures_>=security::MaxAttempts){lockFailures_=0;lockWaitUntil_=std::chrono::steady_clock::now()+std::chrono::seconds(security::LockoutSeconds);lockMessage_=QString("Too many tries \u2014 wait %1 seconds").arg(security::LockoutSeconds);}
  else lockMessage_="Wrong passcode \u2014 try again";lockPaint(true);return true;}
 lockFailures_=0;
 if(lock_==Lock::Verify&&lockThenChoose_){lockThenChoose_=false;lock_=Lock::Choose;lockPaint(true);return true;}
 if(lock_==Lock::Verify){try{security::clear(data_);finishLock();notice("Passcode off");}catch(...){finishLock();notice("Passcode not changed");}return true;}
 finishLock();return true;}
void Shell::lockTick(){
 // A suspend shows as CLOCK_BOOTTIME moving ahead of CLOCK_MONOTONIC: if the tablet slept without the display raising
 // the lock first, raise it on the first tick after waking.
 timespec boot{},mono{};clock_gettime(CLOCK_BOOTTIME,&boot);clock_gettime(CLOCK_MONOTONIC,&mono);const std::int64_t s=(std::int64_t(boot.tv_sec)-mono.tv_sec)*1000000000LL+(boot.tv_nsec-mono.tv_nsec);
 const bool slept=s-suspendedNs_>2000000000LL;suspendedNs_=s;if(slept&&libraryMode_&&lock_!=Lock::Unlock&&passcodeSet()){recovery::v2::e2::mark("power:lock-backstop");beginLock(Lock::Unlock);}
 if(lock_!=Lock::None&&lockWaitUntil_!=std::chrono::steady_clock::time_point{}&&std::chrono::steady_clock::now()>=lockWaitUntil_){lockWaitUntil_={};lockMessage_.clear();lockPaint(true);}}
bool Shell::lockForSleep(){if(!libraryMode_||lock_==Lock::Unlock||!passcodeSet())return false;beginLock(Lock::Unlock);return true;}
// ---- Selection (one system for Highlight, Define, Note, Ask Tom and Copy) -------------------------------------------
// A Selection-tool drag selects the complete touched source blocks. Handles still adjust exact word edges. A drag that starts on an end of the current selection (its handle)
// moves that end, keeping the other; with walkIn an end just beside the text walks in to the first word.
semantic::ScreenRangeResult Shell::selectRange(int sx,int sy,int ex,int ey,bool walkIn,bool handlesOnly){
 const auto geometry=semantics_->geometry();
 if(const auto current=semantics_->selection();current&&semantics_->view()==semantic::View::Menu){
  const auto q=current->data["quads"].toArray(),m=current->data["transform"].toArray();
  if(!q.isEmpty()&&m.size()==6){auto map=[&](double x,double y){return std::pair<double,double>{m[0].toDouble()*x+m[2].toDouble()*y+m[4].toDouble(),m[1].toDouble()*x+m[3].toDouble()*y+m[5].toDouble()};};
   const auto a=q.first().toArray(),b=q.last().toArray();
   const auto [hx,hy]=map(a[0].toDouble()+1,(a[1].toDouble()+a[5].toDouble())/2);const auto [kx,ky]=map(b[2].toDouble()-1,(b[3].toDouble()+b[7].toDouble())/2);
   auto near=[&](double x,double y){return std::hypot(sx-x,sy-y)<72;};
   if(near(hx,hy)||near(kx,ky)){const bool first=near(hx,hy);auto ranges=semantic::selectionRanges(current->data);if(ranges.isEmpty())return {semantic::RangeStatus::InvalidGeometry,{},0,0,false};auto edge=ranges[first?0:ranges.size()-1].toArray();const int offset=edge[first?1:0].toInt()-(first?1:0);std::pair<double,double> fixed=first?std::pair{kx,ky}:std::pair{hx,hy};for(auto value:geometry->data["chars"].toArray()){auto c=value.toObject();int start=c["offset"].toInt(),end=start+c["text"].toString().size();if(offset>=start&&offset<end){auto quad=c["quad"].toArray();fixed=map((quad[0].toDouble()+quad[6].toDouble())/2,(quad[1].toDouble()+quad[7].toDouble())/2);break;}}auto result=semantic::selectScreenRangeChecked(geometry,fixed.first,fixed.second,ex,ey,version(),walkIn);if(result.selection)result.selection=semantic::replaceSelectionEdge(geometry,current,result.selection,first,version());result.replace=true;return result;}}}
 if(handlesOnly)return {semantic::RangeStatus::StartMiss,{},0,0};return semantic::selectScreenRangeChecked(geometry,sx,sy,ex,ey,version(),walkIn,(selectMode_&&std::hypot(ex-sx,ey-sy)>25)||(styled_&&styled_->style().tool==styled::Tool::Highlighter&&!selectMode_&&std::abs(ey-sy)>48&&std::abs(ey-sy)>2*std::abs(ex-sx)));}
// The selection tool with the pen: when the contact lifts, the words between its ends (or the word under a tap) are
// selected exactly as a finger drag selects them, and the selection strip offers its actions. Nothing is drawn.
void Shell::selectTick(){if(!document_||document_->contact()||penSelect_.active||stop_)return;const unsigned count=penRangeCount_;penRangeCount_=0;const bool overflow=penRangeOverflow_;penRangeOverflow_=false;if(!selectMode_||(state_!=State::Clean&&state_!=State::Chrome)){for(auto& pending:penRanges_)pending={};return;}auto lap=std::chrono::steady_clock::now();for(unsigned n=0;n<count;++n){const auto& pen=penRanges_[n];if(pen.epoch!=version()||pen.first.eraser||pen.last.eraser)continue;
 if(pen.action!=-1){if(pen.action>=0&&!pen.first.eraser&&pen.selected==semantics_->selection()&&std::abs(pen.screenLast.x-pen.screenFirst.x)<=25&&std::abs(pen.screenLast.y-pen.screenFirst.y)<=25&&semanticVisual_.targets.find(pen.screenLast.x,pen.screenLast.y)==pen.action)(void)semanticAction(pen.screenLast.x,pen.screenLast.y);if(!selectMode_)break;continue;}
if(state_==State::Chrome&&(topStrip().contains(pen.first.x,pen.first.y)||ib::reader::toolbarRect().contains(pen.first.x,pen.first.y)))continue;const auto result=selectRange(pen.first.x,pen.first.y,pen.last.x,pen.last.y,true);if(result.selection){if(semantics_->select(result.selection,!result.replace))recovery::v2::e2::mark("ui:select-glyphs",result.glyphCount);else notice("Select fewer or shorter passages");}else if(!semantics_->hasSelection())semantics_->notify(QString::fromUtf8(semantic::rangeStatusText(result.status)));}for(auto& pending:penRanges_)pending={};if(overflow)notice("Select fewer passages — the previous selection is kept");if(semantics_->takeDirty())semanticPaint();if(count)recovery::v2::e2::mark("ui:select-visible-us",lapUs(lap));}
// Every notice names itself in the journal (a stable id of its text, and the text once on stderr): UI strings only.
void Shell::markNotice(const QString& text){if(text.isEmpty()){recovery::v2::e2::mark("ui:notice-cleared");return;}const unsigned id=unsigned(qHash(text,0x1b7a));recovery::v2::e2::mark("ui:notice-set",id);std::fprintf(stderr,"NOTICE_TEXT id=%u text=%s\n",id,text.toUtf8().constData());}
// Snap highlights to text (the highlighter palette). After a highlighter stroke is saved, the words between its two ends are
// selected exactly as a finger drag in highlighter mode selects them; if there are any, the stroke is replaced by a text
// highlight in the highlighter's colour (product op highlight.snap: one store transaction, one undo step) off the shell
// thread, and styled ink recomposes. No text under the stroke, another stroke in between, a black highlighter or any
// refusal leaves the freehand mark exactly as drawn.
void Shell::snapTick(){
 if(snapThread_.joinable()&&snapDone_.load(std::memory_order_acquire)){snapThread_.join();if(snapOk_.load()&&styled_&&styled_->reload())expectRemoval(); /* the freehand stroke went away */}
 if(!snapStroke_.complete||snapThread_.joinable()||!styled_||!document_||document_->contact()||stop_)return;
 const auto status=styled_->status();if(status.busy||!status.ready)return; /* wait until the stroke is in the store */
 const auto stroke=snapStroke_;snapStroke_={};
 try{
  const auto doc=styled_->snapshot()["documents"].toObject()[selectedId_].toObject();const auto strokes=doc["strokes"].toArray();if(strokes.isEmpty())return;
  const auto last=strokes.last().toObject();if(last["style"].toObject()["tool"]!="highlighter")return;
  const auto& prepared=document_->active().prepared;styled::Matrix matrix;const auto values=prepared.metadata["pageToSurface"].toArray();if(values.size()!=6)return;for(int i=0;i<6;++i)matrix.value[std::size_t(i)]=values[i].toDouble();
  const auto first=last["points"].toArray().first().toArray();double fx{},fy{};if(!matrix.map(first[0].toDouble(),first[1].toDouble(),fx,fy)||std::abs(fx-stroke.first.x)>6||std::abs(fy-stroke.first.y)>6)return; /* not the stroke just drawn */
  // Strokes usually start and end just beside the words: the range walks in from each end to the first word.
  const auto range=semantic::selectScreenRangeChecked(semantics_->geometry(),stroke.first.x,stroke.first.y,stroke.last.x,stroke.last.y,version(),true,std::abs(stroke.last.y-stroke.first.y)>48&&std::abs(stroke.last.y-stroke.first.y)>2*std::abs(stroke.last.x-stroke.first.x));if(!range.selection)return;
  const auto book=std::find_if(books_.begin(),books_.end(),[&](const auto& d){return d.id==selectedId_;});if(book==books_.end())return;
  const auto anchor=notes::selectedAnchor(product::pageSnapshot(*book,prepared),range.selection->data);
  const QString color=QString::fromLatin1(styled::colorId(styled_->highlighterColor()));if(color.isEmpty()||color=="black")return;
  const QJsonObject command{{"op","highlight.snap"},{"document",selectedId_},{"stroke",last["id"]},{"anchor",anchor},{"color",color}};const auto directory=product::featureDirectory(data_,*book);
  snapOk_=false;snapDone_=false;
  snapThread_=std::thread([this,directory,command]{
   for(unsigned n=0;;++n){try{product::Services services(directory);(void)services.perform(command);snapOk_=true;break;}
    catch(const std::exception& e){if(std::string_view(e.what())!="product-store-already-open"||n>=2000||stop_.load())break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}catch(...){break;}}
   snapDone_.store(true,std::memory_order_release);});
 }catch(...){}
}
bool Shell::reserve()noexcept{if(lock_!=Lock::None||!writable())return false;
 if(selectMode_&&styled_&&(state_==State::Clean||state_==State::Chrome)&&!(tom_&&tom_->active())&&!(notes_&&notes_->active())&&!toolsView_.active()&&!viewControls_.active()){penSelect_={};penSelect_.active=true;penSelect_.epoch=version();return true;}if(quietEffectsEnabled()||!reveal_.quiescent())cancelRevealForContact();if(tom_&&tom_->active()){if(!tom_->reserve())return false;if(document_&&!document_->beginExternalContact()){tom_->cancel();return false;}return true;}if(notes_&&notes_->active()){if(!notes_->reserve())return false;if(!document_->beginExternalContact()){notes_->cancel();return false;}return true;}if(!styled_)return document_->reserve();if(!styled_->reserve())return false;if(!document_->beginExternalContact()){styled_->cancel();return false;}return true;}
bool Shell::append(const spike::Point& original)noexcept{auto p=original;if(epubZoomActive()&&!statusContact_&&p.y<2060){auto at=contentPoint(p.x,p.y);p.x=at.first;p.y=at.second;}if(stop_)return false;if(statusContact_){statusLast_=p;if(p.phase==spike::Phase::Up){statusContact_=false;statusTapPending_=!p.eraser&&std::abs(p.x-statusFirst_.x)<=25&&std::abs(p.y-statusFirst_.y)<=25;}return true;}if(p.phase==spike::Phase::Down&&libraryMode_&&document_&&!(tom_&&tom_->active())&&!(notes_&&notes_->active())&&statusBarRect_.contains(p.x,p.y)){if(penSelect_.active)penSelect_={};else if(styled_){styled_->cancel();document_->endExternalContact();}statusFirst_=statusLast_=p;statusContact_=true;return true;}
 // reserve() cannot know which end of the Marker will touch. Switch a hardware
 // eraser contact to the ordinary ink owner before collecting selection points.
 if(penSelect_.active&&p.phase==spike::Phase::Down&&p.eraser){if(quietEffectsEnabled()||!reveal_.quiescent())cancelRevealForContact();penSelect_={};if(!styled_->reserve())return false;if(!document_->beginExternalContact()){styled_->cancel();return false;}}
 if(penSelect_.active){if(p.phase==spike::Phase::Down){penSelect_.first=p;penSelect_.screenFirst=original;
  // An explicit Selection-tool contact may tap the existing selection UI.
  // Capture intent only; dictionary, storage and panel work run after pen-up.
  if(semantics_->view()!=semantic::View::None&&(semanticVisual_.menu.contains(original.x,original.y)||semanticVisual_.panel.contains(original.x,original.y))){penSelect_.action=original.eraser?-2:semanticVisual_.targets.find(original.x,original.y);if(penSelect_.action<0)penSelect_.action=-2;penSelect_.selected=semantics_->selection();}}
 penSelect_.screenLast=original;penSelect_.last=p;if(p.phase==spike::Phase::Up){penSelect_.active=false;penSelect_.complete=true;if(penRangeCount_<penRanges_.size())penRanges_[penRangeCount_++]=penSelect_;else penRangeOverflow_=true;penSelect_={};}return true;} /* no ink: the end points only */if(tom_&&tom_->active()){bool ok=tom_->append(p);if(document_&&(!ok||p.phase==spike::Phase::Up))document_->endExternalContact();if(!ok)fail(15);return ok;}if(!document_)return false;if(notes_&&notes_->active()){bool ok=notes_->append(p);if(!ok||p.phase==spike::Phase::Up)document_->endExternalContact();if(!ok)fail(16);return ok;}if(!styled_)return document_->append(p);bool ok=styled_->append(p);if(!ok){styled_->cancel();document_->endExternalContact();fail(17);snapStroke_={};}else{
  // Snap highlights to text: only the stroke's endpoints are noted here (contact path: no allocation, no I/O);
  // snapTick() does the work once the stroke is saved and the pen is up.
  if(p.phase==spike::Phase::Down)snapStroke_={p,p,snapHighlights_&&!p.eraser&&!styled_->eraser()&&styled_->style().tool==styled::Tool::Highlighter,false};
  else if(snapStroke_.active){snapStroke_.last=p;if(p.phase==spike::Phase::Up)snapStroke_.complete=true;}
  if(p.phase==spike::Phase::Up){document_->endExternalContact();if(p.eraser||styled_->eraser())expectRemoval();}}
 return ok;}
void Shell::cancel()noexcept{statusContact_=statusTapPending_=false;if(penSelect_.active){penSelect_={};return;}if(tom_&&tom_->active())tom_->cancel();if(notes_&&notes_->active())notes_->cancel();if(styled_)styled_->cancel();if(document_)document_->cancel();}
void Shell::beginQuiesce()noexcept{
 if(quiescing_)return;quiescing_=true;stop_=true;light_.beginQuiesce();covers_.beginQuiesce();companionStop_=true;wifiStop_=true;powerConstrained_=true;
 statusContact_=statusTapPending_=false;shutdownInterrupted_=(document_&&document_->contact())||(notes_&&notes_->contact())||(tom_&&tom_->contact());
 // Signal all domains before polling any slow helper. Canvas stop must precede
 // ordinary cancellation so interrupted Notes/Tom input remains explicit.
 if(tom_)(void)tom_->finish(0);
 if(notes_)(void)notes_->finish(0);
 if(styled_)(void)styled_->finish(0);
 (void)semantics_->finish(0);
 if(document_)document_->beginQuiesce();
 thumbnails_.cancel();
}
Shell::Shutdown Shell::pollQuiesce()noexcept{
 beginQuiesce();(void)light_.tick();(void)covers_.collect();
 if(tom_)(void)tom_->finish(0);
 if(notes_)(void)notes_->finish(0);
 if(styled_)(void)styled_->finish(0);
 (void)semantics_->finish(0); // Reconcile the bounded output mailbox while stopping.
 if(companionThread_.joinable()&&companionDone_.load(std::memory_order_acquire)){companionThread_.join();companionBusy_=false;}
 if(wifiThread_.joinable()&&wifiDone_.load(std::memory_order_acquire)){wifiThread_.join();wifiBusy_=false;}
 if(opener_.joinable()&&done_.load(std::memory_order_acquire)){opener_.join();if(!finishOpener())failed_=true;}
 (void)thumbnails_.collect(); // a cancelled thumbnail helper is reaped here; it never writes anything
 if(snapThread_.joinable()&&snapDone_.load(std::memory_order_acquire))snapThread_.join(); // a snap ends quickly once stop_ is set
 const bool writersDrained=(!tom_||tom_->drained())&&(!notes_||notes_->drained())&&(!styled_||styled_->drained())&&semantics_->drained();
 const bool helpersDrained=light_.drained()&&covers_.drained()&&!companionThread_.joinable()&&!wifiThread_.joinable()&&!opener_.joinable()&&thumbnails_.drained()&&!snapThread_.joinable()&&(!preparationTracking_.ledger||openerReconciled_.load(std::memory_order_acquire));
 // A failed document open or display is not an unsaved write. Preserve its
 // independent failed outcome while checking every persistence owner here.
 const bool canonicalSaved=!shutdownInterrupted_&&!semantics_->failed()&&(!styled_||!styled_->status().failed)&&(!notes_||!notes_->failed())&&(!tom_||!tom_->failed())&&unsaved()==0&&semantics_->pending()==0;
 bool documentDrained=true;if(document_)documentDrained=document_->pollQuiesce(writersDrained&&helpersDrained,canonicalSaved);
 const bool drained=writersDrained&&helpersDrained&&documentDrained;
 const bool saved=drained&&canonicalSaved&&(!document_||document_->shutdownSaved());
 return {drained,saved,shutdownInterrupted_||failed()};
}
// 1 tom, 2 notes, 4 styled, 8 semantics writers; 16 companion, 32 opener threads; 64 opener unreconciled; 128 unsaved,
// 256 semantics pending, 512 interrupted; 1024 document not drained, 2048 document not saved.
unsigned Shell::drainBlockers()noexcept{unsigned m=0;
 if(tom_&&!tom_->drained())m|=1u;if(notes_&&!notes_->drained())m|=2u;if(styled_&&!styled_->drained())m|=4u;if(!semantics_->drained())m|=8u;
 if(!light_.drained()||!covers_.drained()||companionThread_.joinable()||wifiThread_.joinable()||!thumbnails_.drained()||snapThread_.joinable())m|=16u;if(opener_.joinable())m|=32u;if(preparationTracking_.ledger&&!openerReconciled_.load(std::memory_order_acquire))m|=64u;
 if(unsaved())m|=128u;if(semantics_->pending())m|=256u;if(shutdownInterrupted_)m|=512u;
 if(document_&&!document_->pollQuiesce(!(m&127u),!(m&(128u|256u|512u))))m|=1024u;if(document_&&!document_->shutdownSaved())m|=2048u;return m;}
bool Shell::finish(unsigned ms)noexcept{
 beginQuiesce();const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
 do{auto state=pollQuiesce();if(state.drained)return state.saved;if(std::chrono::steady_clock::now()>=end)return false;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(true);
}
}

namespace ux {
void Shell::composeZoom(spike::Surface out,spike::Rect r)const{
 auto part=ib::intersect(r,{0,0,1620,2060});if(!part.empty()&&zoomPixels_.size()==underlay::Pixels){const auto source=epubZoom_.source(part);spike::Surface scratch{zoomPixels_,1620,2160,1632};if(styled_)styled_->composite(scratch,clean_,source);else underlay::compose(scratch,clean_,const_cast<Shell*>(this)->ink(),source);
  for(auto ui:{ib::reader::toolHandleRect(),ib::reader::ZoomBadgeRect}){auto patch=ib::intersect(ui,source);if(!patch.empty()){const auto& base=document_->active().prepared.background;if(styled_)styled_->composite(scratch,base,patch);else underlay::compose(scratch,base,const_cast<Shell*>(this)->ink(),patch);}}
  for(int y=part.y0;y<part.y1;++y)for(int x=part.x0;x<part.x1;++x){auto p=epubZoom_.inverse(x+.5,y+.5);const int sx=std::clamp(int(std::floor(p.first)),0,1619),sy=std::clamp(int(std::floor(p.second)),0,2059);out.pixels[std::size_t(y)*out.stridePixels+x]=(x<4||x>=1616||y<4||y>=2056)?0xff000000u:zoomPixels_[std::size_t(sy)*1632+sx];}
  for(auto ui:{ib::reader::toolHandleRect(),ib::reader::ZoomBadgeRect}){if(ui.x0==ib::reader::toolHandleRect().x0&&!const_cast<Shell*>(this)->toolHandleShown())continue;auto patch=ib::intersect(ui,part);if(patch.empty())continue;for(int y=patch.y0;y<patch.y1;++y)std::copy_n(clean_.data()+std::size_t(y)*1632+patch.x0,patch.x1-patch.x0,out.pixels.data()+std::size_t(y)*out.stridePixels+patch.x0);}
 }
 auto footer=ib::intersect(r,{0,2060,1620,2160});if(!footer.empty()){if(styled_)styled_->composite(out,background(),footer);else underlay::compose(out,background(),const_cast<Shell*>(this)->ink(),footer);}
}
}
