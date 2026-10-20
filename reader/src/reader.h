#pragma once
#include "work_tracking.h"
#include "underlay.h"
#include "library.h"
#include "chrome.h"
namespace reader {
QJsonObject companionCommand(const std::filesystem::path&,const std::filesystem::path&,QString action,QString expectedPairId,QString expectedAccount,const std::atomic<bool>& stop);
void syncLibrary(const std::filesystem::path&,const std::filesystem::path&,const std::atomic<bool>&);
// Imports one app-generated file (e.g. a new notebook) through the same isolated helper.
void importFile(const std::filesystem::path& app,const std::filesystem::path& data,const std::filesystem::path& file,const std::atomic<bool>&);
// Reader-only move mailbox. Unlike the qualified POD input queue, consumed
// shared ownership is cleared on the consumer thread, never retained in a slot.
template<class T,std::size_t N>class Mailbox {
 std::array<T,N> values_{};std::atomic<std::size_t> head_{0},tail_{0};
public:
 bool push(T&& v){auto h=head_.load(std::memory_order_relaxed),n=(h+1)%N;if(n==tail_.load(std::memory_order_acquire))return false;values_[h]=std::move(v);head_.store(n,std::memory_order_release);return true;}
 bool push(const T& v){T copy=v;return push(std::move(copy));}
 bool pop(T& v){auto t=tail_.load(std::memory_order_relaxed);if(t==head_.load(std::memory_order_acquire))return false;v=std::move(values_[t]);values_[t]=T{};tail_.store((t+1)%N,std::memory_order_release);return true;}
};
enum class State {Closed,Opening,Preparing,Ready,Active,Leaving,Cached,Closing,Failed};
enum class NavigationRefusal {None,Busy,Unavailable};
struct Page {
 std::filesystem::path storage;underlay::Prepared prepared;std::unique_ptr<underlay::Pipeline> saves;int index{};
 bool idle()const{return saves->queued.load()==0&&saves->unsaved()==0;}
};
struct Resume {int page{};std::uint64_t generation{};bool clean{};};
std::filesystem::path pagePath(const std::filesystem::path&,int);
Resume readResume(const std::filesystem::path&,int);
void writeResume(const std::filesystem::path&,int,int,std::uint64_t,bool);
std::filesystem::path pageStoragePath(const std::filesystem::path&,int,const QString&,unsigned orientation=0);
Resume readViewResume(const std::filesystem::path&,int,const QString&);
void writeViewResume(const std::filesystem::path&,int,int,std::uint64_t,bool,const QString&);
// orientation: 0, or 90/270 for a PDF page shown turned (docs/LANDSCAPE.md).
underlay::Prepared prepareViewPage(const std::filesystem::path&,const std::filesystem::path&,int,const std::atomic<bool>&,const QString&,unsigned orientation=0);
underlay::Prepared initialPrepare(const std::filesystem::path&,const std::filesystem::path&);
underlay::Prepared preparePage(const std::filesystem::path&,const std::filesystem::path&,int,const std::atomic<bool>&);
class Reader final:public spike::OperationSink {
 std::unique_ptr<library::Lease> libraryLease_;
 struct Request {int page{};bool resume{},clean{};recovery::v2::progress::WorkToken tracking;};
 struct Result {int page{};std::shared_ptr<Page> value;bool failed{},reconciled{true},skipped{};recovery::v2::progress::WorkToken tracking;};
 lifecycle::Tracking preparationTracking_,resumeTracking_;
 std::array<recovery::v2::progress::WorkToken,10> cancelledPreparations_{},deferredResumes_{};
 unsigned cancelledPreparationCount_{},deferredResumeCount_{};recovery::v2::progress::WorkToken finalResumeTracking_;
 bool enqueue(Request);void reconcile(Result&)noexcept;
 std::filesystem::path application_,data_;QString viewSet_;unsigned orientation_{};int total_{},wanted_{-1},activatedPage_{};std::uint64_t generation_{},view_{1};
 std::array<std::shared_ptr<Page>,3> cache_{};std::array<std::shared_ptr<Page>,4> closingPages_{};std::shared_ptr<Page> active_,previous_;
 std::array<bool,1000> pending_{},errors_{};
 Mailbox<Request,9> requests_;Mailbox<Result,9> results_;Mailbox<std::shared_ptr<Page>,9> retired_;
 std::thread preparer_;std::atomic<bool> stop_{false},done_{false},preparationStopped_{false},writing_{false},preparing_{false},workerError_{false},helperDispositionKnown_{true};
 std::shared_ptr<std::atomic<unsigned>> resident_;std::atomic<int> foregroundPage_{-1},prefetchCenter_{0};std::atomic<unsigned> backgroundYields_{0};
 State state_{State::Opening};bool exit_{},quiescing_{},interrupted_{};unsigned navRejected_{};NavigationRefusal navRefusal_{NavigationRefusal::None};
 std::atomic<int> closingPage_{0};std::atomic<bool> flushSignalled_{false},cleanClose_{false};unsigned retiredCommits_{};
 void work()noexcept;void requestPage(int);bool cacheAdmissible(int)const;bool room(int);void prefetch();
 std::shared_ptr<Page> makePage(underlay::Prepared);
public:
 // orientation: the PDF pages are shown turned (90/270, docs/LANDSCAPE.md); every page it prepares is turned the same way.
 Reader(const std::filesystem::path&,const std::filesystem::path&,underlay::Prepared,QString viewSet={},lifecycle::Tracking preparationTracking={},lifecycle::Tracking resumeTracking={},unsigned orientation=0);
 unsigned orientation()const noexcept{return orientation_;}
 ~Reader();bool request(Action,bool markerContact=false);bool navigateTo(int page,bool markerContact=false);bool tick();
 bool reserve()noexcept override;bool append(const spike::Point&)noexcept override;void cancel()noexcept override;
 // External canonical ink owns persistence; Reader retains contact/navigation gating.
 bool beginExternalContact()noexcept{return writable()&&!writing_.exchange(true);}
 void endExternalContact()noexcept{writing_=false;}
 // Stop every page writer before waiting; release the final resume fence once.
 void beginQuiesce()noexcept;
 bool pollQuiesce(bool canonicalDrained,bool canonicalClean)noexcept;
 bool drained()const noexcept{return !preparer_.joinable();}
 bool shutdownSaved()const noexcept{return drained()&&cleanClose_.load()&&!workerError_.load()&&!interrupted_&&unsaved()==0;}
 bool finish(unsigned ms=100,bool canonicalClean=true)noexcept;
 Page& active(){return *active_;}int page()const{return active_->index;}int total()const{return total_;}
 bool writable()const{return state_==State::Active&&!exit_;}
 // Ink admission: writable and the page saver can take a stroke now. When it cannot (all save
 // slots busy, or the page is full) a new pen-down is skipped by the session gate instead of
 // being refused by reserve(), which would end the session.
 std::size_t inkRoom()const{return active_&&active_->saves?active_->saves->room():0;}
 bool inkAdmissible()const{return writable()&&active_&&active_->saves&&active_->saves->canReserve();}bool exiting()const{return exit_;}
 bool contact()const{return writing_.load();}bool preparing()const{return preparing_.load();}
 State state()const{return state_;}std::uint64_t view()const{return view_;}void installed(){previous_.reset();}
 void activate(){activatedPage_=page();prefetchCenter_=page();foregroundPage_=-1;state_=State::Active;prefetch();}void fail(){workerError_=true;exit_=true;state_=State::Failed;}
 NavigationRefusal takeNavigationRefusal()noexcept{auto result=navRefusal_;navRefusal_=NavigationRefusal::None;return result;}
 unsigned resident()const{return resident_->load();}unsigned backgroundYields()const{return backgroundYields_.load();}unsigned rejectedNavigation()const{return navRejected_;}
 unsigned unsaved()const;unsigned committed()const;bool failed()const{return workerError_.load();}
};
}
