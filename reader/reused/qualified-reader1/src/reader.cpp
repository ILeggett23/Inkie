#include "reader.h"
#include <QJsonDocument>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace reader {
std::filesystem::path pagePath(const std::filesystem::path& root,int page){if(page<0||page>=1000)throw std::runtime_error("page-index");return root/underlay::FixtureHash/("page-"+std::to_string(page));}
Resume readResume(const std::filesystem::path& root,int total){
 try{auto o=QJsonDocument::fromJson(underlay::readBounded(root/"resume.json",4096)).object();auto payload=o["payload"].toObject();auto bytes=QJsonDocument(payload).toJson(QJsonDocument::Compact);
  if(o["checksum"].toString().toLatin1()!=underlay::digest(bytes).toHex()||payload["version"].toInt()!=1||payload["document"].toString()!=underlay::FixtureHash||payload["render"].toString()!="reader-footer100-v1"||payload["total"].toInt()!=total)return {};
  auto page=payload["page"].toInt(-1);bool ok=false;auto gen=payload["generation"].toString().toULongLong(&ok);if(page<0||page>=total||!ok||gen>1000000000)return {};return {page,gen,payload["clean"].toBool()};
 }catch(...){return {};}
}
void writeResume(const std::filesystem::path& root,int page,int total,std::uint64_t generation,bool clean){
 QJsonObject p{{"version",1},{"document",underlay::FixtureHash},{"render","reader-footer100-v1"},{"page",page},{"total",total},{"generation",QString::number(generation)},{"clean",clean}};
 auto b=QJsonDocument(p).toJson(QJsonDocument::Compact);underlay::atomicFile(root/"resume.json",QJsonDocument(QJsonObject{{"payload",p},{"checksum",QString::fromLatin1(underlay::digest(b).toHex())}}).toJson(QJsonDocument::Compact));
}
std::shared_ptr<Page> Reader::makePage(underlay::Prepared p){
 if(resident_->load()>=4)throw std::runtime_error("resident-capacity");auto* raw=new Page;raw->index=p.metadata["page"].toInt();raw->prepared=std::move(p);
 try{raw->saves=std::make_unique<underlay::Pipeline>(pagePath(data_,raw->index)/"annotations.ink",raw->prepared);}catch(...){delete raw;throw;}
 ++*resident_;auto count=resident_;return std::shared_ptr<Page>(raw,[count](Page* value){delete value;--*count;});
}
Reader::Reader(const std::filesystem::path& app,const std::filesystem::path& data,underlay::Prepared p):application_(app),data_(data),total_(p.metadata["pages"].toInt()),resident_(std::make_shared<std::atomic<unsigned>>(0)){
 if(total_<1||total_>1000)throw std::runtime_error("page-count");generation_=readResume(data,total_).generation+1;active_=makePage(std::move(p));cache_[0]=active_;state_=State::Ready;requests_.push({page(),true,false});preparer_=std::thread([this]{work();});
}
Reader::~Reader(){if(!finish())std::_Exit(78);}
void Reader::requestPage(int p){if(p<0||p>=total_||pending_[p]||errors_[p])return;for(auto& c:cache_)if(c&&c->index==p)return;if(requests_.push({p,false,false}))pending_[p]=true;}
void Reader::prefetch(){if(writable()&&!contact()){requestPage(page()+1);requestPage(page()-1);}}
bool Reader::room(int incoming){for(auto& c:cache_)if(!c)return true;for(auto& c:cache_)if(c!=active_&&c!=previous_&&c->idle()&&std::abs(c->index-page())>=std::abs(incoming-page())){auto n=c->saves->committed.load();if(!retired_.push(std::move(c)))return false;retiredCommits_+=n;return true;}return false;}
bool Reader::request(Action a,bool markerContact){
 if(a==Action::None)return false;if(markerContact||contact()){++navRejected_;return false;}
 if(a==Action::Exit){exit_=true;state_=State::Closing;return true;}
 if(state_!=State::Active){++navRejected_;return false;}int target=page()+(a==Action::Next?1:-1);if(target<0||target>=total_||errors_[target]){++navRejected_;return false;}
 wanted_=target;state_=State::Preparing;requestPage(target);return true;
}
bool Reader::tick(){
 if(contact())return false;if(workerError_){fail();return false;}Result result;
 while(results_.pop(result)){pending_[result.page]=false;if(result.failed){errors_[result.page]=true;if(wanted_==result.page){wanted_=-1;state_=State::Active;}continue;}
  if(!room(result.page)){if(!retired_.push(std::move(result.value))){workerError_=true;exit_=true;}continue;}
  for(auto& c:cache_)if(!c){c=std::move(result.value);break;}
 }
 if(wanted_>=0){for(auto& c:cache_)if(c&&c->index==wanted_){if(!requests_.push({wanted_,true,false}))return false;state_=State::Leaving;previous_=active_;active_=c;wanted_=-1;++view_;state_=State::Ready;return true;}requestPage(wanted_);}
 prefetch();return false;
}
bool Reader::reserve()noexcept{if(!writable())return false;writing_=true;if(!active_->saves->reserve()){writing_=false;return false;}return true;}
bool Reader::append(const spike::Point& p)noexcept{bool ok=active_->saves->append(p);if(p.phase==spike::Phase::Up)writing_=false;return ok;}
void Reader::cancel()noexcept{active_->saves->cancel();writing_=false;}
void Reader::work()noexcept{
 if(setpriority(PRIO_PROCESS,static_cast<id_t>(syscall(SYS_gettid)),19)!=0){workerError_=true;stop_=true;}
 Request req;std::shared_ptr<Page> retired;
 while(!stop_){while(retired_.pop(retired))retired.reset();if(!requests_.pop(req)){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
  if(req.resume){try{writeResume(data_,req.page,total_,generation_,req.clean);}catch(...){workerError_=true;}continue;}
  while(!stop_&&(writing_||resident_->load()>=4)){while(retired_.pop(retired))retired.reset();std::this_thread::sleep_for(std::chrono::milliseconds(2));}if(stop_)break;
  preparing_=true;Result out;out.page=req.page;try{out.value=makePage(preparePage(application_,data_,req.page,stop_));}catch(...){out.failed=true;}
  preparing_=false;while(!stop_&&!results_.push(std::move(out)))std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
 while(retired_.pop(retired))retired.reset();while(!flushSignalled_)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 try{writeResume(data_,closingPage_,total_,generation_,cleanClose_&&!workerError_);}catch(...){workerError_=true;}done_=true;
}
unsigned Reader::unsaved()const{unsigned n=0;for(auto& c:cache_)if(c)n+=c->saves->unsaved();return n;}
unsigned Reader::committed()const{unsigned n=retiredCommits_;for(auto& c:cache_)if(c)n+=c->saves->committed;return n;}
bool Reader::finish(unsigned ms)noexcept{
 if(!preparer_.joinable())return true;auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);state_=State::Closing;cancel();closingPage_=page();stop_=true;bool flushed=true;
 for(auto& c:cache_)if(c){auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now()).count();if(!c->saves->finish(unsigned(std::max<std::int64_t>(0,remaining))))flushed=false;}
 cleanClose_=flushed&&unsaved()==0;flushSignalled_=true;
 while(!done_&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));if(!done_||!flushed)return false;preparer_.join();state_=State::Closed;return true;
}
}
