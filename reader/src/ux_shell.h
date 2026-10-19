#pragma once
#include "device_status.h"
#include "refresh_policy.h"
#include "reader.h"
#include <algorithm>
#include <utility>
#include "library.h"
#include "library_view.h"
#include "library_manage.h"
#include "companion_view.h"
#include "wifi_view.h"
#include "sleep_settings.h"
#include "time_settings.h"
#include "timezone_view.h"
#include "touch.h"
#include "text_input.h"
#include <mutex>
#include <chrono>
#include <optional>
#include "styled_ink.h"
#include "tools_view.h"
#include "localized_reveal.h"
#include "notes_panel.h"
#include "note_favorites.h"
#include "bookmarks.h"
#include "tom_panel.h"
#include "document_controls.h"
#include "page_thumbnails.h"
#include "semantic_range.h"
#include "ui_prefs.h"
#include "passcode.h"
#include "ib/lock_screen.h"
#include "note_indicators.h"
#include "notebook_view.h"
#include "ux_paint.h"
#include <cstdio>
#include <cstring>
#include "semantic_paint.h"
#include "view_zoom.h"
#include "frontlight.h"
#include "book_covers.h"
#include "ib/status_screen.h"
namespace ux {
inline constexpr const char* DataPath="/home/root/.local/share/inkbound/reader-integration";
bool catalogueValid(const std::filesystem::path&)noexcept;
// Reader "More" menu entries (targets in Shell::menuTargets()).
enum MenuId:int {MenuNotes=500,MenuTom,MenuLayers,MenuSettings,MenuPages,MenuLibrary,MenuBookmark,MenuAbout,MenuLight,LightOff,LightLess,LightMore,LightBack};
// cleanup: where content was removed (erase, undo/redo, snap, highlight or layer changes). It was redrawn with the fast
// update; the display gives it one quality refresh once ink is settled and the pen is up, so no colour or grey ghost
// stays. Additions (new strokes) never request it, so writing never flashes.
struct Change {bool full{};unsigned regions{};std::array<spike::Rect,8> extra{};unsigned extraCount{};spike::Rect cleanup{};std::array<spike::Rect,16> uiCleanup{};unsigned uiCleanupCount{};bool content{},preview{},pageTurn{};FullRefreshReason fullReason{FullRefreshReason::None};void complete(FullRefreshReason reason)noexcept{full=true;fullReason=reason;}};
class Shell final:public spike::OperationSink {
 friend struct StatusTestAccess;friend struct QolTestAccess;
 clockprefs::Setting timeSetting_;TimezoneView timezoneView_;void timezonePaint();bool timezoneFinger(int,int);
 ib::status::Model statusModel_;bool quickSettings_{},quickSettingsWait_{},statusContact_{},statusTapPending_{};std::chrono::steady_clock::time_point quickSettingsUntil_{};bool panelShown()const noexcept{return quickSettings_&&!quickSettingsWait_;}spike::Point statusFirst_{},statusLast_{};std::vector<std::uint32_t> statusBarPixels_;spike::Rect statusBarRect_{};ib::Targets statusTargets_;qint64 clockMinute_{-1};
 bool openQuickSettings();bool statusFinger(int,int);void refreshStatus(bool force=false);void pollClock(qint64);void compositeStatus(spike::Surface,spike::Rect)const noexcept;
 device::frontlight::Control light_;int pendingLight_{-1};covers::Service covers_;void coverTick();reader::lifecycle::Tracking canonicalTracking_,notesTracking_,tomTracking_,preparationTracking_,resumeTracking_;recovery::v2::progress::WorkToken openerTracking_,openerResumeTracking_;
 std::filesystem::path app_,data_,selectedData_;
 bool releaseNetworking_{}; // release::policy(app).networking, read once at construction (it was re-read every tick)
 bool libraryMode_{},companionEnabled_{true},tomNetworkEnabled_{true},returning_{},syncing_{};LibraryView libraryView_;LibraryManage libraryManage_;unsigned libraryOffset_{};QVector<library::Document> books_;QString selectedId_,selectedTitle_,selectedFormat_,currentViewSet_,openingViewSet_;std::unique_ptr<library::Lease> openingLease_;void libraryPaint();void reloadLibrary();
 // Repaints the library and refreshes only the bands that changed (a popup, a row, the keyboard), not the whole panel.
 std::vector<std::uint32_t> libraryBefore_;void libraryRefresh();
 NoteFavorites noteFavorites_; // starred notes, shared by the library Notes section and the notes panel
 static constexpr spike::Rect LightAnchor{-5,-5,-4,-4};
 static constexpr spike::Rect RecentsAnchor{-9,-9,-8,-8}; // menuAnchor_ sentinel: the menu overlay shows Recents
 static constexpr spike::Rect QuickBrowseAnchor{-7,-7,-6,-6}; // menuAnchor_ sentinel: the overlay is Quick browse
 bool quickBrowsing()const noexcept{return menu_&&menuAnchor_.x0==QuickBrowseAnchor.x0&&menuAnchor_.y0==QuickBrowseAnchor.y0;}int quickBrowseStart_{};bool quickBrowseTap(int id,int x);
 QVector<QString> recentIds_;QString pendingOpen_; // the book to open once the reader is back in the library
 Bookmarks bookmarks_; // page bookmarks (top bar toggle; listed in Document > Pages)
 QString bookmarkLayout()const{if(!document_)return {};if(selectedFormat_!="epub")return QStringLiteral("pdf");auto l=document_->active().prepared.metadata["layout"].toString();return l.isEmpty()?QStringLiteral("epub-base"):l;}

 CompanionView companionView_;QJsonObject companionStatus_{{"state","unpaired"}};std::thread companionThread_;std::atomic<bool> companionDone_{false},companionStop_{false};QJsonObject companionResult_;bool companionBusy_{};
 std::chrono::steady_clock::time_point nextCompanion_{},nextInbox_{};bool inboxRunning_{};
 // Inbox listings (names, inode, size, times): last observed, at the start of the running pass, at the last completed pass.
 std::string inboxSeen_,inboxStarted_,inboxPassed_;bool inboxSeenValid_{},inboxPassedValid_{};std::optional<std::string> inboxSignature()const;bool startCompanion(QString,QString={},QString={});void companionTick();
 // D2 library Wi-Fi screen: scans and connects on a background thread (wpa_supplicant control socket).
 WifiView wifiView_;TextInput wifiPassword_;std::thread wifiThread_;std::atomic<bool> wifiDone_{false},wifiStop_{false};bool wifiBusy_{};int wifiSelected_{-1};
 // The chosen network is kept by value: a scan finishing while its password is typed may re-sort the rows.
 std::optional<wifi::Network> wifiChosen_;wifi::Requests wifiRequests_;
 std::mutex wifiMutex_;std::vector<wifi::Network> wifiFound_;wifi::Status wifiFoundStatus_;QString wifiResult_;
 std::chrono::steady_clock::time_point nextWifi_{};
 void startWifi(int selected,QString password);void runWifi(std::optional<wifi::Network>,QString,bool automatic);void wifiTick();void wifiPaint();
 std::unique_ptr<reader::Reader> document_;
 ViewZoom epubZoom_;mutable std::vector<std::uint32_t> zoomPixels_;
 bool epubZoomActive()const noexcept{return document_&&selectedFormat_=="epub"&&epubZoom_.active()&&!(tom_&&tom_->active())&&!(notes_&&notes_->active());}
 std::pair<int,int> contentPoint(int x,int y)const noexcept{if(!epubZoomActive())return {x,y};auto p=epubZoom_.inverse(x,y);return {int(std::lround(p.first)),int(std::lround(p.second))};}
 spike::Rect contentRect(spike::Rect r)const noexcept{return epubZoomActive()?epubZoom_.damage(r):r;}
 void composeZoom(spike::Surface,spike::Rect)const;
 std::vector<std::uint32_t> pdfPreviewPixels_;std::array<double,6> pdfPreviewToOld_{};bool pdfPreviewActive_{};bool beginPdfPreview(const reader::pdfview::Plan&);void composePdfPreview(spike::Surface,spike::Rect)const;

 std::unique_ptr<styled::Ink> styled_;unsigned retiredStyledCommits_{};QByteArray styledPage_;std::vector<std::uint32_t> engineScratch_,toolsPixels_;ToolsView toolsView_;std::uint64_t toolsRevision_{1};
 mutable localized::Reveal reveal_;localized::Update revealUpdate_;mutable std::uint64_t revealPaintStamp_{};std::uint64_t revealUiEpoch_{1},revealOpeningEpoch_{};int revealView_{-1};QString revealSelection_;bool reducedMotion_{},powerConstrained_{},libraryEffects_{};
 product::AnimationPolicy revealPolicy(bool contact=false)const noexcept;void revealTick();void revealSceneChanged();void cancelRevealForContact()noexcept;void compositeReveal(spike::Surface,spike::Rect)const noexcept;void revealCovered(spike::Rect)noexcept;
 Action pendingAction_{Action::None}; /* Exit waits for the existing save drain. */
 int navigationIntent_{-1},navigationSubmitted_{-1};std::uint64_t navigationSequence_{},navigationOperation_{},navigationActiveOperation_{};std::chrono::steady_clock::time_point navigationRetryAt_{};
 bool dispatchNavigation();void clearNavigation(); /* One accumulated target and one submitted page, never a queue of swipes. */
 bool semanticLate_{}; // D2: page shown before its semantic data loaded; paint it on arrival
 DocumentControls viewControls_;std::vector<std::uint32_t> viewPixels_;bool changingView_{},restoringBookmark_{},viewportRefresh_{};QString bookmarkView_;reader::pdfview::View requestedView_;DocumentControls::EpubSettings requestedEpubView_;int viewPage_{};reader::pdfview::Rect viewBounds_;
 DocumentControls::Snapshot viewSnapshot()const;bool openViewControls();void viewTick();bool jumpBookmark(int);
 std::unique_ptr<tom::Panel> tom_;State tomReturn_{State::Picker};QJsonObject tomSource_;mutable tom::PresentationStamp tomPaintStamp_{};std::uint64_t tomEpochStamp_{},tomGestureStamp_{};
 QString tomDestinationNote_;QJsonObject tomDestinationBinding_;bool openTom(semantic::Selection={},QString note={});void tomClosed();void tomTick();void sourceTick();
 std::unique_ptr<notes::Panel> notes_;std::vector<notes::Indicator> noteIndicators_;QJsonObject noteSource_;bool notesReload_{},notesClosing_{};
 bool notesRetiring_{},tomRetiring_{};bool trimInactivePanel(bool targetTom);
 bool openNotes(notes::Mode,semantic::Selection={},QString={},QJsonObject expectedNative={});void notesTick();QJsonObject pendingNativeOutput_;
 ToolsView::Snapshot toolsSnapshot()const;void toolsPaint();bool bindStyled();
 std::vector<std::uint32_t> clean_,chrome_;
 std::vector<std::uint32_t> semanticOverlay_;std::unique_ptr<semantic::Controller> semantics_;semantic::Visual semanticVisual_{};
 bool awaitingSemanticPage_{};std::uint64_t presentedVersion_{1};
 std::span<std::uint32_t> presentedInk_{};
 State state_{State::Picker};Change change_{};std::array<char,32> counter_{};
 power::SleepSettings sleep_;bool sleepLoaded_{};unsigned sleepRevision_{};
 bool pageFirstPending_{};std::chrono::steady_clock::time_point pageFirstAt_{}; /* page-first: first styled result after install */
 bool userEnded_{};std::thread opener_;std::atomic<bool> stop_{false},done_{false},openFailed_{false},openerReconciled_{true},openerPositionKnown_{true};std::unique_ptr<underlay::Prepared> prepared_;
 bool finishOpener()noexcept{const bool helperKnown=openerReconciled_.load(std::memory_order_acquire),positionKnown=openerPositionKnown_.load(std::memory_order_acquire);bool helper=preparationTracking_.complete(openerTracking_,helperKnown),position=resumeTracking_.complete(openerResumeTracking_,positionKnown);return helper&&position&&(!preparationTracking_.ledger||helperKnown)&&(!resumeTracking_.ledger||positionKnown);}
 template<class F>void startOpener(F&& work){openerReconciled_=true;openerPositionKnown_=true;try{opener_=std::thread(std::forward<F>(work));}catch(...){preparationTracking_.complete(openerTracking_,false);resumeTracking_.complete(openerResumeTracking_,false);throw;}}
 bool failed_{},loadingShown_{},quiescing_{},shutdownInterrupted_{};unsigned rejected_{};
 void pageBackgrounds();
 struct UiScene {semantic::View view{semantic::View::None};bool definitionOnly{},definitionExpanded{};unsigned definitionOffset{};QString selected,notice;QJsonObject definition;QByteArray page;std::array<spike::Rect,6> semanticRegions{};unsigned semanticCount{};spike::Rect semanticMenu{},semanticPanel{},tools{},menu{},palette{},status{};unsigned paletteStyle{},statusStyle{};};
 UiScene submittedUi_{},pendingUi_{};std::array<spike::Rect,16> pendingUiRegions_{};unsigned pendingUiCount_{};std::uint16_t pendingUiCovered_{};UiScene uiScene()const;void coverUi(spike::Rect)noexcept;
 // Inkbound chrome
 ib::Targets cleanTargets_,chromeTargets_,menuTargets_;bool menu_{},selectMode_{},selectionChrome_{},loadingChrome_{};spike::Rect menuRect_{};
 ib::reader::Tools paintedTools_;std::string paintedStyledStatus_;bool toolsPainted_{};
 std::array<styled::Style,2> toolShortcuts_{{{styled::Tool::Ballpoint,styled::Width::Medium,0},{styled::Tool::Highlighter,styled::Width::Medium,4}}};unsigned activeShortcut_{};bool shortcutsLoaded_{},shortcutsStored_{},shortcutApplyPending_{true};void syncToolShortcuts();void rememberShortcut();void saveToolShortcuts();QString notice_;QSet<QString> favorites_;
 struct PendingNote {QString document,note;QJsonObject anchor;} pendingNote_;spike::Rect menuAnchor_{};
 ib::reader::Chrome chromeModel()const;ib::reader::Tools railModel()const;void paintChrome(bool force=false);void paintMenu();bool toolHandle_{true},toolHandleLoaded_{};bool toolHandleShown()noexcept;void setToolHandle(bool); /* the collapsed toolbar handle (ui-toolbar.json) */bool leftHanded()noexcept;void setLeftHanded(bool); /* toolbar side: right for left-handed writers (ui-toolbar.json) */void saveToolbarPrefs();bool leftHanded_{};
 /* Reading preferences (Settings > Reading, ui-toolbar.json): the quiet progress line under the page, and highlighter
    strokes over text snapping to a text highlight. */
 bool readingProgress_{true},snapHighlights_{true};public:bool readingProgress()noexcept{toolHandleShown();return readingProgress_;}bool snapHighlights()noexcept{toolHandleShown();return snapHighlights_;}void setReadingProgress(bool);void setSnapHighlights(bool);private:
 /* Settings > Tom and Accessibility (ui-prefs.json) and Security (passcode lock, ui-passcode.json). While a passcode
    screen is up nothing else takes input or ink; the reader keeps saving underneath. */
 Prefs prefs_;void applyPrefs();void savePrefs();
 enum class Lock {None,Unlock,Choose,Confirm,Verify};Lock lock_{Lock::None};bool lockThenChoose_{};QString lockEntry_,lockFirst_,lockMessage_;int lockFailures_{};
 std::chrono::steady_clock::time_point lockWaitUntil_{};std::vector<std::uint32_t> lockPixels_;ib::Targets lockTargets_;std::int64_t suspendedNs_{};
 void beginLock(Lock);void lockPaint(bool full=true);bool lockTap(int x,int y);void finishLock();void lockTick();
 public:bool locked()const noexcept{return lock_!=Lock::None;}const ib::Targets& lockTargets()const noexcept{return lockTargets_;}const Prefs& prefs()const noexcept{return prefs_;}
 bool passcodeSet()const noexcept{return bool(security::load(data_));}
 // Display, just before it releases the wake lock: true when the passcode screen was raised and must be presented
 // now, so the e-ink panel shows it (not the page) while the tablet sleeps.
 bool lockForSleep();private:
 struct SnapStroke {spike::Point first,last;bool active{},complete{};};
 bool cleanupNext_{},notesStyledDirty_{};void expectRemoval()noexcept{cleanupNext_=true;} /* the next ink result removes something: request a cleanup */
 // Selection tool with the pen: the contact selects text instead of writing (no ink, nothing saved).
 // probe: a Marker contact while a selection or definition is up without the Selection tool. On the strip or card it
 // is a tap (the action runs after pen-up); anywhere else it becomes ordinary ink at pen-down.
 struct PenSelect {spike::Point first,last;bool active{},complete{},probe{};std::uint64_t epoch{};spike::Point screenFirst,screenLast;int action{-1};semantic::Selection selected;};PenSelect penSelect_;std::array<PenSelect,semantic::MaxSelectionRanges> penRanges_{};unsigned penRangeCount_{};bool penRangeOverflow_{};void selectTick();
 semantic::ScreenRangeResult selectRange(int sx,int sy,int ex,int ey,bool walkIn,bool handlesOnly=false); /* drags that start on a selection end move that end */
 void markNotice(const QString&);bool retireFailedTom();bool retireFailedNotes();std::atomic<bool> viewRefused_{false}; /* the last layout change was refused; the old layout reopened */void setLibraryNotice(QString text){markNotice(text);libraryView_.setNotice(std::move(text));}SnapStroke snapStroke_;std::thread snapThread_;std::atomic<bool> snapDone_{false},snapOk_{false};void snapTick();bool chromeStale_{}; /* chrome_ is painted when it becomes visible, not on every page turn */
 bool railAction(int);bool menuAction(int);bool semanticAction(int x,int y);bool semanticCommand(int id);bool onSemantic(int x,int y)const noexcept;int semanticTarget(int x,int y)const noexcept;
 // A strip action refused only because ink is still saving or a contact is open runs once that settles (same selection).
 struct PendingSelectionAction {int id{-1};semantic::Selection selected;};PendingSelectionAction pendingSelectionAction_;void selectionActionTick();void notice(QString);void libraryNotes();void saveFavorites();
 NotebookView notebook_;bool creatingNotebook_{};QString notebookFolder_;QSet<QString> idsBeforeImport_;
 bool openSettings(int tab);void viewPaint(bool full=true);void thumbnailTick();thumbs::Service thumbnails_;bool openNewNotebook();bool startImport();void backdrop(std::vector<std::uint32_t>&);std::vector<std::uint32_t> backdrop_;semantic::Selection tomSelection_;QJsonObject pendingTomNote_;
 void semanticPaint();void damage(spike::Rect);void tomDamage(spike::Rect);
public:
 Shell(const std::filesystem::path&,const std::filesystem::path&,bool libraryMode=false,bool companionEnabled=true,bool tomNetworkEnabled=true,reader::lifecycle::Tracking canonicalTracking={},reader::lifecycle::Tracking notesTracking={},reader::lifecycle::Tracking tomTracking={},reader::lifecycle::Tracking preparationTracking={},reader::lifecycle::Tracking resumeTracking={});~Shell();
 QVector<QString> visibleLibraryIds()const{return libraryView_.visibleIds();}
 const QVector<library::Document>& libraryDocuments()const{return books_;}QString currentDocumentId()const{return selectedId_;}std::filesystem::path libraryDataRoot()const{return data_;}
 void setReducedMotion(bool value)noexcept{reducedMotion_=value;}void setPowerConstrained(bool value)noexcept{powerConstrained_=value;}
 bool quietEffectsEnabled()const noexcept{return styled_?styled_->revealEnabled():libraryEffects_;}bool revealPending()const noexcept{return !reveal_.quiescent();}
 bool viewPreviewActive()const noexcept{return pdfPreviewActive_;}
 bool applyPdfView(reader::pdfview::View,bool viewportOnly=false);
 bool applyEpubView(DocumentControls::EpubSettings);
 // Recents (top-edge swipe): in the library, the Recents section; in a book, a sheet from the top
 // edge listing recent books. libraryPage: swipe paging in the library. Entry points for gesture routing.
 bool openRecents();bool openQuickBrowse();bool libraryPage(int delta);
 // reMarkable-style finger gestures (touch.h): swipe pages, two/three-finger tap undo/redo, top-edge swipe to recents.
 bool gesture(reader::Gesture);
 bool gesture(const reader::Touch::Tap&); // carries pinch scale/focal point and two-finger pan deltas
 reader::pdfview::View pdfView()const{return viewSnapshot().pdf;} // the applied PDF view (zoom, pan) of this page
 void pollDevice();device::Battery battery_{};std::optional<bool> wifiLink_;device::Storage storage_{};std::string firmware_;std::chrono::steady_clock::time_point batteryAt_{};bool lowBatteryShown_{};
 bool pinch(float scale,int focalX,int focalY);bool twoFingerPan(int dx,int dy,int originY);bool resetZoom();double pdfZoom();QString zoomKey_;double zoom_{1};
 bool goHome();bool returnToLibrary();bool navigateTo(int);bool choose(unsigned);bool action(Action,bool markerContact=false);bool tick();void installed();
 bool finger(int x,int y,bool longPress=false,bool markerContact=false);
 std::array<spike::Rect,2> selectionHandles()const;bool selectionEnabled()const noexcept{if(!stop_&&lock_==Lock::None&&!quickSettings_&&!wifiView_.active()&&tom_&&tom_->active())return tom_->panAvailable();if(!stop_&&lock_==Lock::None&&!quickSettings_&&!wifiView_.active()&&notes_&&notes_->active()&&document_&&document_->writable())return notes_->selectionEnabled();return !quickSettings_&&!wifiView_.active()&&styled_&&((semantics_->view()==semantic::View::None&&(styled_->style().tool==styled::Tool::Highlighter||selectMode_))||(semantics_->view()==semantic::View::Menu&&semantics_->hasSelection()))&&(state_==State::Clean||state_==State::Chrome)&&document_&&document_->writable()&&styled_->status().ready&&!styled_->status().failed&&!toolsView_.active()&&!viewControls_.active()&&(!notes_||!notes_->active())&&(!tom_||!tom_->active());}
 bool toolbarVisible()const noexcept{return document_&&styled_&&(state_==State::Chrome||(state_==State::NavigationPending&&!chromeStale_))&&!(notes_&&notes_->active())&&!(tom_&&tom_->active());}spike::Rect touchToolbar()const noexcept{return toolbarVisible()?ib::reader::toolbarRect():spike::Rect{};}reader::DragMode touchMode()const noexcept;bool touchDrag(const reader::Touch::Tap&);bool selectionDrag(int startX,int startY,int endX,int endY,std::uint64_t epoch);
 semantic::Controller& semantics(){return *semantics_;}
 semantic::Visual semanticVisual()const{return semanticVisual_;}
 void compositeTransient(spike::Surface out,spike::Rect damage)const noexcept{semantic::compositeOverlay(out,semanticOverlay_,semanticVisual_,damage);}
 // Inkbound chrome targets: clean state (footer, page edges) and chrome state (top bar, rail, footer).
 const ib::Targets& chromeTargets()const noexcept{return showsClean()?cleanTargets_:chromeTargets_;}
 const thumbs::Service& thumbnails()const noexcept{return thumbnails_;}
 const ib::Targets& menuTargets()const noexcept{return menuTargets_;}const DocumentControls& viewControls()const noexcept{return viewControls_;}const ToolsView& toolsView()const noexcept{return toolsView_;}
 const LibraryView& libraryView()const noexcept{return libraryView_;}const LibraryManage& libraryManage()const noexcept{return libraryManage_;}const NotebookView& notebookView()const noexcept{return notebook_;}bool menuOpen()const noexcept{return menu_;}
 bool selectMode()const noexcept{return selectMode_;}QString clipboard()const{return sharedClipboard();}QString notice()const{return notice_;}
 std::span<std::uint32_t> pageComparisonScratch()noexcept{return styled_&&document_&&!document_->contact()&&!penSelect_.active&&!statusContact_?std::span<std::uint32_t>(engineScratch_):std::span<std::uint32_t>{};}
 bool reserve()noexcept override;bool append(const spike::Point&)noexcept override;void cancel()noexcept override;
 struct Shutdown {bool drained{},saved{},failed{};};
 void beginQuiesce()noexcept;Shutdown pollQuiesce()noexcept;
 bool finish(unsigned ms=100)noexcept;
 // E2 diagnostics: bit mask of what still blocks a drained/saved shutdown (see ux_shell.cpp).
 unsigned drainBlockers()noexcept;
 std::size_t inkRoom()const noexcept{if(penSelect_.active||statusContact_)return 1u<<16;if(tom_&&tom_->active())return tom_->inkRoom();if(notes_&&notes_->active())return notes_->inkRoom();if(styled_)return styled_->room();return document_?document_->inkRoom():0;}
 // The page's current ink is coloured (not black): its strokes get their own local quality update once the pen pauses.
 bool colorInk()const noexcept;
 bool eraseMode()const noexcept{if(tom_&&tom_->active())return tom_->localEraser();if(notes_&&notes_->active())return notes_->localEraser();return styled_&&styled_->eraser();}
 bool cleanupSettled()const noexcept{return inkSettled()&&(!notes_||!notes_->active()||notes_->renderSettled())&&(!tom_||!tom_->active()||tom_->renderSettled());}
 bool generatedPlaying()const noexcept{return (notes_&&notes_->active()&&notes_->generatedPlaying())||(tom_&&tom_->active()&&tom_->generatedPlaying());}bool generatedQuiet()const noexcept{return (!notes_||!notes_->active()||notes_->generatedQuiet())&&(!tom_||!tom_->active()||tom_->generatedQuiet());}
 bool inkSettled()const noexcept{return !styled_||!styled_->status().busy;} // D2: no ink save/redraw pending
 bool inkAdmission()const noexcept{if(!writable())return false;if((tom_&&tom_->active())||(notes_&&notes_->active())||styled_)return true;return document_&&document_->inkAdmissible();} // styled ink is gated inside writable()
 // One view replacement (a sheet's change, a library repaint) is presented as one region: separate quality updates
 // of its pieces finish one after another on the panel.
 template<std::size_t N> static spike::Rect joined(const std::array<spike::Rect,N>& r,unsigned n)noexcept{spike::Rect out{};for(unsigned i=0;i<n&&i<N;++i){if(r[i].empty())continue;if(out.empty())out=r[i];else out.include(r[i]);}return out;}
 bool chromeBeforeHeld_{}; // libraryBefore_ holds the chrome as shown when a rail tap began (toolsPaint diffs against it)
 bool exitAfterNavigation_{}; // the close was tapped while a page turned: return to the library when it settles
 // A library sheet (or the lock) owns the screen and its taps: the library under it must not repaint over it. Each
 // sheet repaints the library when it closes, so notices posted meanwhile appear then.
 bool librarySheetOpen()const noexcept{return libraryManage_.active()||companionView_.active()||notebook_.active()||wifiView_.active()||timezoneView_.active()||lock_!=Lock::None||(tom_&&tom_->active());}
 // Ink admission must agree with reserve(): the session treats a refused reserve after an admitted contact as fatal,
 // so every condition reserve() checks before accepting a contact is checked here first (the lock, and the reader's
 // external-contact gate behind Notes and Tom).
 bool writable()const noexcept{if(stop_||lock_!=Lock::None)return false;if(timezoneView_.active()||viewControls_.active()||quickSettings_||wifiView_.active())return false;if(tom_&&tom_->active())return tom_->writable()&&(!document_||document_->writable());if(notes_&&notes_->active())return !notesClosing_&&notes_->writable()&&document_&&document_->writable();return document_&&(state_==State::Clean||state_==State::Chrome)&&document_->writable()&&(!styled_||styled_->admissionOpen());}
 // D2 user-ended sessions (V2 reader): an on-screen Exit never ends the session (only the power button does); in a
 // document it returns to the library, in the library it shows how to leave.
 void setUserEnded(bool v)noexcept{userEnded_=v;}bool userEnded()const noexcept{return userEnded_;}bool statusAvailable()const noexcept{return libraryMode_&&!timezoneView_.active()&&!stop_&&!returning_&&!changingView_&&lock_==Lock::None&&!wifiView_.active()&&!(notes_&&notes_->active())&&!(tom_&&tom_->active())&&!libraryManage_.active()&&!companionView_.active()&&!notebook_.active()&&((!document_&&state_==State::Picker)||(document_&&(state_==State::Clean||state_==State::Chrome||state_==State::NavigationPending)));}
 // Inkbound's sleep settings (cover / idle), loaded from the data root on first use; the display applies them to
 // the session's power policy whenever sleepRevision() changes.
 const power::SleepSettings& sleepSettings()noexcept{if(!sleepLoaded_){sleep_=power::load(data_);sleepLoaded_=true;}return sleep_;}
 bool setSleepSettings(power::SleepSettings s)noexcept{if(!power::save(data_,s))return false;sleep_=s;sleepLoaded_=true;++sleepRevision_;return true;}
 unsigned sleepRevision()const noexcept{return sleepRevision_;}void prepareSleep()noexcept{covers_.cancel();thumbnails_.cancel();if(notes_&&notes_->active())notes_->prepareSleep();}
 bool exiting()const noexcept{return stop_||state_==State::ExitPending||state_==State::Failed;}
 bool failed()const noexcept{return failed_||(document_&&document_->failed())||semantics_->failed()||(styled_&&styled_->status().failed)||(notes_&&notes_->failed()&&notes_->unsaved()!=0)||(tom_&&tom_->failed()&&tom_->unsaved()!=0);}void fail(unsigned reason=0)noexcept; /* reason: call-site code, see docs/hardware-validation.md "Shell failure reasons" */
 spike::Surface ink()noexcept{if(tom_&&tom_->active())return tom_->ink();return {styled_?std::span<std::uint32_t>(engineScratch_):presentedInk_.empty()?std::span<std::uint32_t>(clean_):presentedInk_,underlay::Width,underlay::Height,underlay::Stride};}
 bool showsClean()const noexcept{return state_==State::Clean||(chromeStale_&&state_==State::NavigationPending);}
 std::span<const std::uint32_t> background()const noexcept{return showsClean()?std::span<const std::uint32_t>(clean_):std::span<const std::uint32_t>(chrome_);}
 State state()const noexcept{return state_;}std::uint64_t version()const noexcept{return presentedVersion_;}
 int page()const noexcept{return document_?document_->page():-1;}const char* counter()const{return counter_.data();}
 unsigned committed()const{return retiredStyledCommits_+(document_?document_->committed():0)+(styled_?styled_->status().saved:0);}unsigned unsaved()const{return (document_?document_->unsaved():0)+(styled_?styled_->unsaved():0)+(notes_?notes_->unsaved():0)+(tom_?tom_->unsaved():0);}unsigned rejected()const{return rejected_;}
 Change takeChange();
 std::span<const std::uint32_t> visibleInk()const noexcept{return styled_?styled_->pixels():std::span<const std::uint32_t>(presentedInk_);}
 double inputScale()const noexcept{return epubZoomActive()?epubZoom_.scale():1.;}
 spike::Rect presentationDamage(spike::Rect r)const noexcept{if(tom_&&tom_->active()){r.include(tom_->inkDamage());return r;}if(notes_&&notes_->active())return notes_->presentationDamage(r);if(styled_)r.include(contentRect(styled_->damage()));return r;}
 void uiPresentationAccepted()noexcept{const auto regions=pendingUiRegions_;const auto count=pendingUiCount_;for(unsigned i=0;i<count;++i)presentationCovered(regions[i]);submittedUi_=pendingUi_;pendingUiCount_=0;pendingUiCovered_=0;} /* all original regions delivered as the union of fast and quality pieces; retire matching panel generations too */
 void presentationAccepted()noexcept{coverUi({0,0,1620,2160});revealCovered({0,0,1620,2160});if(tom_&&tom_->active()){tom_->presentationCovered({0,0,1620,2160},tomPaintStamp_);return;}if(notes_&&notes_->active())notes_->presentationAccepted();else if(styled_)styled_->clearDamage();}
 void presentationCovered(spike::Rect r)noexcept{coverUi(r);revealCovered(r);if(tom_&&tom_->active()){tom_->presentationCovered(r,tomPaintStamp_);return;}if(notes_&&notes_->active()){notes_->presentationCovered(r);return;}if(styled_){auto p=contentRect(styled_->damage());if(!p.empty()&&r.x0<=p.x0&&r.y0<=p.y0&&r.x1>=p.x1&&r.y1>=p.y1)styled_->clearDamage();}}
 void compose(spike::Surface out,spike::Rect r)const {if(timezoneView_.active()&&lock_==Lock::None){for(int y=r.y0;y<r.y1;++y)std::copy_n(viewPixels_.data()+std::size_t(y)*1632+r.x0,r.x1-r.x0,out.pixels.data()+std::size_t(y)*out.stridePixels+r.x0);return;}if(lock_!=Lock::None&&lockPixels_.size()>=underlay::Pixels&&!r.empty()&&r.x0>=0&&r.y0>=0&&r.x1<=underlay::Width&&r.y1<=underlay::Height){for(int y=r.y0;y<r.y1;++y)std::copy_n(lockPixels_.data()+std::size_t(y)*underlay::Stride+r.x0,r.x1-r.x0,out.pixels.data()+std::size_t(y)*out.stridePixels+r.x0);return;}if(wifiView_.active()){auto bg=document_?std::span<const std::uint32_t>(viewPixels_):std::span<const std::uint32_t>(chrome_);for(int y=r.y0;y<r.y1;++y)std::copy_n(bg.data()+std::size_t(y)*underlay::Stride+r.x0,r.x1-r.x0,out.pixels.data()+std::size_t(y)*out.stridePixels+r.x0);return;}if(tom_&&tom_->active()){tomPaintStamp_=tom_->presentationStamp();tom_->composite(out,r);compositeReveal(out,r);return;}if(notes_&&notes_->active()){notes_->composite(out,r);compositeReveal(out,r);return;}if(styled_&&document_&&styledPage_!=document_->active().prepared.binding){auto& current=document_->active().prepared;underlay::compose(out,current.background,{current.ink,underlay::Width,underlay::Height,underlay::Stride},r);compositeStatus(out,r);return;}if(pdfPreviewActive_)composePdfPreview(out,r);else if(epubZoomActive())composeZoom(out,r);else if(styled_)styled_->composite(out,background(),r);else if(state_==State::Picker&&!document_&&!r.empty()&&r.x0>=0&&r.y0>=0&&r.x1<=underlay::Width&&r.y1<=underlay::Height&&out.width==underlay::Width&&out.height==underlay::Height&&out.stridePixels==underlay::Stride&&out.pixels.size()>=underlay::Pixels){ /* Library: no document, so no ink layer; the library image is the frame (one stream, not a per-pixel ink blend). */auto bg=background();for(int y=r.y0;y<r.y1;++y)std::copy_n(bg.data()+std::size_t(y)*underlay::Stride+r.x0,r.x1-r.x0,out.pixels.data()+std::size_t(y)*out.stridePixels+r.x0);}else underlay::compose(out,background(),const_cast<Shell*>(this)->ink(),r);if(document_&&selectedFormat_=="pdf"&&zoom_>1.001){auto part=ib::intersect(r,{0,0,1620,2060});for(int y=part.y0;y<part.y1;++y)for(int x=part.x0;x<part.x1;++x)if(x<4||x>=1616||y<4||y>=2056)out.pixels[std::size_t(y)*out.stridePixels+x]=0xff000000u;}compositeTransient(out,r);if(toolbarVisible()&&!chromeStale_){for(auto p:{ib::reader::toolbarRect(),ib::reader::closeRect()}){p=ib::intersect(p,r);for(int y=p.y0;y<p.y1;++y)std::copy_n(chrome_.data()+std::size_t(y)*1632+p.x0,p.x1-p.x0,out.pixels.data()+std::size_t(y)*out.stridePixels+p.x0);}}if(menu_&&!chromeStale_){auto p=ib::intersect(menuRect_,r);for(int y=p.y0;y<p.y1;++y)std::copy_n(chrome_.data()+std::size_t(y)*1632+p.x0,p.x1-p.x0,out.pixels.data()+std::size_t(y)*out.stridePixels+p.x0);}if(state_==State::Clean&&styled_&&toolHandle_){auto p=ib::intersect(ib::reader::toolHandleRect(),r);for(int y=p.y0;y<p.y1;++y)std::copy_n(clean_.data()+std::size_t(y)*1632+p.x0,p.x1-p.x0,out.pixels.data()+std::size_t(y)*out.stridePixels+p.x0);}if(viewControls_.active()){auto p=DocumentControls::Panel;for(int y=std::max(r.y0,p.y0);y<std::min(r.y1,p.y1);++y)for(int x=std::max(r.x0,p.x0);x<std::min(r.x1,p.x1);++x)out.pixels[std::size_t(y)*out.stridePixels+x]=viewPixels_[std::size_t(y)*underlay::Stride+x];}if(toolsView_.active()){auto p=ToolsView::panel();int x0=std::max(r.x0,p.x0),x1=std::min(r.x1,p.x1),y0=std::max(r.y0,p.y0),y1=std::min(r.y1,p.y1);for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)out.pixels[std::size_t(y)*out.stridePixels+x]=toolsPixels_[std::size_t(y)*underlay::Stride+x];}compositeReveal(out,r);compositeStatus(out,r);}
 tom::Panel* tomPanel()noexcept{return tom_.get();}
 notes::Panel* notesPanel()noexcept{return notes_.get();}
 styled::Ink* styledInk()noexcept{return styled_.get();}
 reader::Reader* document()noexcept{return document_.get();}
};
}
