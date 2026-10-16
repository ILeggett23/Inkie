#pragma once
#include "reader.h"
#include "ux_paint.h"
namespace ux {
inline constexpr const char* DataPath="/home/root/.local/share/inkbound/reader-ux2";
bool catalogueValid(const std::filesystem::path&)noexcept;
struct Change {bool full{};unsigned regions{};};
class Shell final:public spike::OperationSink {
 std::filesystem::path app_,data_;
 std::unique_ptr<reader::Reader> document_;
 std::vector<std::uint32_t> clean_,chrome_;
 State state_{State::Picker};Change change_{};std::array<char,32> counter_{};
 std::thread opener_;std::atomic<bool> stop_{false},done_{false},openFailed_{false};std::unique_ptr<underlay::Prepared> prepared_;
 bool failed_{},loadingShown_{};unsigned rejected_{};
 void pageBackgrounds();
public:
 Shell(const std::filesystem::path&,const std::filesystem::path&);~Shell();
 bool choose(unsigned);bool action(Action,bool markerContact=false);bool tick();void installed();
 bool reserve()noexcept override;bool append(const spike::Point&)noexcept override;void cancel()noexcept override;
 bool finish(unsigned ms=100)noexcept;
 bool writable()const noexcept{return document_&&(state_==State::Clean||state_==State::Chrome)&&document_->writable();}
 bool exiting()const noexcept{return state_==State::ExitPending||state_==State::Failed;}
 bool failed()const noexcept{return failed_||(document_&&document_->failed());}void fail()noexcept{failed_=true;state_=State::Failed;}
 spike::Surface ink()noexcept{return {document_?std::span<std::uint32_t>(document_->active().prepared.ink):std::span<std::uint32_t>(clean_),underlay::Width,underlay::Height,underlay::Stride};}
 std::span<const std::uint32_t> background()const noexcept{return state_==State::Clean?std::span<const std::uint32_t>(clean_):std::span<const std::uint32_t>(chrome_);}
 State state()const noexcept{return state_;}std::uint64_t version()const noexcept{return document_?1+document_->view():1;}
 int page()const noexcept{return document_?document_->page():-1;}const char* counter()const{return counter_.data();}
 unsigned committed()const{return document_?document_->committed():0;}unsigned unsaved()const{return document_?document_->unsaved():0;}unsigned rejected()const{return rejected_;}
 Change takeChange()noexcept{auto c=change_;change_={};return c;}
 reader::Reader* document()noexcept{return document_.get();}
};
}
