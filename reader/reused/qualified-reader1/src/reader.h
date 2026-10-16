#pragma once
#include "underlay.h"
#include "chrome.h"
namespace reader {
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
struct Page {
 underlay::Prepared prepared;std::unique_ptr<underlay::Pipeline> saves;int index{};
 bool idle()const{return saves->queued.load()==0&&saves->unsaved()==0;}
};
struct Resume {int page{};std::uint64_t generation{};bool clean{};};
std::filesystem::path pagePath(const std::filesystem::path&,int);
Resume readResume(const std::filesystem::path&,int);
void writeResume(const std::filesystem::path&,int,int,std::uint64_t,bool);
underlay::Prepared initialPrepare(const std::filesystem::path&,const std::filesystem::path&);
underlay::Prepared preparePage(const std::filesystem::path&,const std::filesystem::path&,int,const std::atomic<bool>&);
class Reader final:public spike::OperationSink {
 struct Request {int page{};bool resume{},clean{};};
 struct Result {int page{};std::shared_ptr<Page> value;bool failed{};};
 std::filesystem::path application_,data_;int total_{},wanted_{-1};std::uint64_t generation_{},view_{1};
 std::array<std::shared_ptr<Page>,3> cache_{};std::shared_ptr<Page> active_,previous_;
 std::array<bool,1000> pending_{},errors_{};
 Mailbox<Request,9> requests_;Mailbox<Result,9> results_;Mailbox<std::shared_ptr<Page>,9> retired_;
 std::thread preparer_;std::atomic<bool> stop_{false},done_{false},writing_{false},preparing_{false},workerError_{false};
 std::shared_ptr<std::atomic<unsigned>> resident_;
 State state_{State::Opening};bool exit_{};unsigned navRejected_{};
 std::atomic<int> closingPage_{0};std::atomic<bool> flushSignalled_{false},cleanClose_{false};unsigned retiredCommits_{};
 void work()noexcept;void requestPage(int);bool room(int);void prefetch();
 std::shared_ptr<Page> makePage(underlay::Prepared);
public:
 Reader(const std::filesystem::path&,const std::filesystem::path&,underlay::Prepared);
 ~Reader();bool request(Action,bool markerContact=false);bool tick();
 bool reserve()noexcept override;bool append(const spike::Point&)noexcept override;void cancel()noexcept override;
 bool finish(unsigned ms=100)noexcept;
 Page& active(){return *active_;}int page()const{return active_->index;}int total()const{return total_;}
 bool writable()const{return state_==State::Active&&!exit_;}bool exiting()const{return exit_;}
 bool contact()const{return writing_.load();}bool preparing()const{return preparing_.load();}
 State state()const{return state_;}std::uint64_t view()const{return view_;}void installed(){previous_.reset();}
 void activate(){state_=State::Active;prefetch();}void fail(){workerError_=true;exit_=true;state_=State::Failed;}
 unsigned resident()const{return resident_->load();}unsigned rejectedNavigation()const{return navRejected_;}
 unsigned unsaved()const;unsigned committed()const;bool failed()const{return workerError_.load();}
};
}
