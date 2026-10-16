#include "reader.h"
#include "page_server.h"
#include "e2_provisional_v2.h"
#include "helper_disposition.h"
#include "library.h"
#include "document_view_store.h"
#include "epub_view_store.h"
#include "semantic.h"
#include <QJsonDocument>
#include <QDateTime>
#include <algorithm>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace reader {
std::filesystem::path pagePath(const std::filesystem::path& root,int page){if(page<0||page>=1000)throw std::runtime_error("page-index");auto d=library::descriptor(root);auto id=d.isEmpty()?QString(underlay::FixtureHash):d["id"].toString();return root/id.toStdString()/("page-"+std::to_string(page));}
Resume readResume(const std::filesystem::path& root,int total){
 auto descriptor=library::descriptor(root);bool imported=!descriptor.isEmpty();if(imported&&!std::filesystem::exists(root/"resume.json"))return {};
 try{auto o=QJsonDocument::fromJson(underlay::readBounded(root/"resume.json",4096)).object();auto payload=o["payload"].toObject();auto bytes=QJsonDocument(payload).toJson(QJsonDocument::Compact);
  if(o["checksum"].toString().toLatin1()!=underlay::digest(bytes).toHex()||payload["version"].toInt()!=1||payload["document"].toString()!=(imported?descriptor["id"].toString():QString(underlay::FixtureHash))||payload["render"].toString()!="reader-footer100-v1"||payload["total"].toInt()!=total){if(imported)throw std::runtime_error("resume-identity-or-schema");return {};}
  auto page=payload["page"].toInt(-1);bool ok=false;auto gen=payload["generation"].toString().toULongLong(&ok);if(page<0||page>=total||!ok||gen>1000000000){if(imported)throw std::runtime_error("resume-invalid");return {};}return {page,gen,payload["clean"].toBool()};
 }catch(...){if(imported)throw;return {};}
}
void writeResume(const std::filesystem::path& root,int page,int total,std::uint64_t generation,bool clean){
 auto descriptor=library::descriptor(root);QJsonObject p{{"version",1},{"document",descriptor.isEmpty()?QString(underlay::FixtureHash):descriptor["id"].toString()},{"render","reader-footer100-v1"},{"page",page},{"total",total},{"generation",QString::number(generation)},{"clean",clean}};
 if(!descriptor.isEmpty())p["opened"]=QString::number(QDateTime::currentSecsSinceEpoch());
 auto b=QJsonDocument(p).toJson(QJsonDocument::Compact);underlay::atomicFile(root/"resume.json",QJsonDocument(QJsonObject{{"payload",p},{"checksum",QString::fromLatin1(underlay::digest(b).toHex())}}).toJson(QJsonDocument::Compact));
}
std::shared_ptr<Page> Reader::makePage(underlay::Prepared p){
 if(resident_->load()>=4)throw std::runtime_error("resident-capacity");auto holder=std::make_unique<Page>();auto* raw=holder.get();raw->index=p.metadata["page"].toInt();raw->storage=pageStoragePath(data_,raw->index,viewSet_);if(!viewSet_.isEmpty()){if(library::descriptor(data_)["format"]=="epub"){auto set=epubviews::readSet(data_,viewSet_);if(p.metadata.value("view_id").toString()!=set.id||p.metadata["layout"]!=set.receipt["layout_id"]||p.metadata["pages"]!=set.receipt["pages"])throw std::runtime_error("epub-page-view-result-mismatch");}else{auto set=viewstore::readSet(data_,viewSet_);if(p.metadata.value("view_id").toString()!=viewstore::pageConfigId(set,raw->index))throw std::runtime_error("page-view-result-mismatch");}}p.text=semantic::loadGeometry(raw->storage/"semantic-text.json",p.binding,p.metadata.contains("document")?p.metadata["document"].toString():QString(underlay::FixtureHash));raw->prepared=std::move(p);
 raw->saves=std::make_unique<underlay::Pipeline>(raw->storage/"annotations.ink",raw->prepared);
 ++*resident_;auto count=resident_;holder.release();return std::shared_ptr<Page>(raw,[count](Page* value){delete value;--*count;});
}
Reader::Reader(const std::filesystem::path& app,const std::filesystem::path& data,underlay::Prepared p,QString viewSet,lifecycle::Tracking preparationTracking,lifecycle::Tracking resumeTracking):preparationTracking_(preparationTracking),resumeTracking_(resumeTracking),application_(app),data_(data),viewSet_(std::move(viewSet)),total_(p.metadata["pages"].toInt()),resident_(std::make_shared<std::atomic<unsigned>>(0)){
 if(!preparationTracking_.supports(recovery::v2::WorkerKind::CancelableHelperPrefix)||!resumeTracking_.supports(recovery::v2::WorkerKind::DurablePrefix))throw std::runtime_error("reader-progress-kind");
 if(!library::descriptor(data).isEmpty())libraryLease_=std::make_unique<library::Lease>(data);
 if(total_<1||total_>1000)throw std::runtime_error("page-count");generation_=readViewResume(data,total_,viewSet_).generation+1;active_=makePage(std::move(p));activatedPage_=active_->index;cache_[0]=active_;state_=State::Ready;if(!enqueue({page(),true,false,{}}))throw std::runtime_error("reader-resume-progress-admission");try{preparer_=std::thread([this]{work();});}catch(...){Request abandoned;while(requests_.pop(abandoned))resumeTracking_.complete(abandoned.tracking,false);throw;}
}
Reader::~Reader(){if(!finish())std::_Exit(78);}
bool Reader::enqueue(Request request){auto tracking=request.resume?resumeTracking_:preparationTracking_;if(!tracking.accept(request.tracking)){workerError_=true;return false;}if(requests_.push(std::move(request)))return true;
 // A reservation refused by the mailbox never starts a helper or write.
 if(!tracking.complete(request.tracking,true))workerError_=true;return false;
}
void Reader::reconcile(Result& result)noexcept{if(!result.reconciled&&preparationTracking_.ledger){helperDispositionKnown_=false;workerError_=true;}if(!preparationTracking_.complete(result.tracking,result.reconciled))workerError_=true;}
void Reader::requestPage(int p){if(p<0||p>=total_||pending_[p]||errors_[p])return;for(auto& c:cache_)if(c&&c->index==p)return;if(p!=wanted_&&!cacheAdmissible(p))return;if(enqueue({p,false,false,{}}))pending_[p]=true;}
void Reader::prefetch(){if(writable()&&!contact()){requestPage(page()+1);requestPage(page()-1);}}
bool Reader::cacheAdmissible(int incoming)const{
 for(const auto& c:cache_)if(!c||(c!=active_&&c!=previous_&&c->idle()&&(incoming==wanted_||std::abs(c->index-page())>std::abs(incoming-page()))))return true;
 return false;
}
bool Reader::room(int incoming){
 for(auto& c:cache_)if(!c)return true;
 auto best=cache_.end();int farthest=-1;for(auto i=cache_.begin();i!=cache_.end();++i){const auto& c=*i;const int distance=std::abs(c->index-page());if(c!=active_&&c!=previous_&&c->idle()&&(incoming==wanted_||distance>std::abs(incoming-page()))&&distance>farthest){best=i;farthest=distance;}}
 /* Retain equally near neighbors; evict the farthest safe page rather than the first eligible slot. */
 if(best==cache_.end())return false;auto n=(*best)->saves->committed.load();if(!retired_.push(std::move(*best)))return false;retiredCommits_+=n;return true;
}
bool Reader::request(Action a,bool markerContact){
 if(a==Action::None)return false;if(markerContact||contact()){++navRejected_;return false;}
 if(a==Action::Exit){foregroundPage_=-1;wanted_=-1;navRefusal_=NavigationRefusal::None;exit_=true;state_=State::Closing;return true;}
 return navigateTo(page()+(a==Action::Next?1:-1));
}
bool Reader::navigateTo(int target,bool markerContact){
 { /* E2 timing: requested page and whether it was already prepared (value = page*10 + cached) */ bool ready=false;for(const auto& c:cache_)ready|=c&&c->index==target;recovery::v2::e2::mark("reader:nav-request",unsigned(std::max(0,target))*10u+(ready?1u:0u));}
 if(markerContact||contact()||state_!=State::Active||exit_||target<0||target>=total_||target==page()||errors_[target]){++navRejected_;return false;}
 wanted_=target;navRefusal_=NavigationRefusal::None;
 const bool cached=std::any_of(cache_.begin(),cache_.end(),[&](const auto& c){return c&&c->index==target;});
 if(!cached&&!cacheAdmissible(target)){wanted_=-1;navRefusal_=NavigationRefusal::Busy;++navRejected_;return false;}
 foregroundPage_=target;state_=State::Preparing;requestPage(target);return true;
}
bool Reader::tick(){
 if(exit_||contact())return false;if(workerError_){fail();return false;}Result result;
 while(results_.pop(result)){pending_[result.page]=false;if(result.skipped){reconcile(result);continue;}if(result.failed){errors_[result.page]=true;if(wanted_==result.page){wanted_=-1;state_=State::Active;navRefusal_=NavigationRefusal::Unavailable;++navRejected_;}reconcile(result);continue;}
  if(!room(result.page)){
   const bool demanded=result.page==wanted_;
   if(!retired_.push(std::move(result.value))){workerError_=true;exit_=true;}
   if(demanded){wanted_=-1;state_=State::Active;navRefusal_=NavigationRefusal::Busy;++navRejected_;}
   reconcile(result);continue;
  }
  for(auto& c:cache_)if(!c){c=std::move(result.value);break;}reconcile(result);
 }
 if(wanted_>=0){for(auto& c:cache_)if(c&&c->index==wanted_){if(!enqueue({wanted_,true,false,{}}))return false;state_=State::Leaving;previous_=active_;active_=c;wanted_=-1;++view_;state_=State::Ready;return true;}requestPage(wanted_);}
 prefetch();return false;
}
bool Reader::reserve()noexcept{if(!writable())return false;writing_=true;if(!active_->saves->reserve()){writing_=false;return false;}return true;}
bool Reader::append(const spike::Point& p)noexcept{bool ok=active_->saves->append(p);if(p.phase==spike::Phase::Up)writing_=false;return ok;}
void Reader::cancel()noexcept{active_->saves->cancel();writing_=false;}
void Reader::work()noexcept{
 if(setpriority(PRIO_PROCESS,static_cast<id_t>(syscall(SYS_gettid)),19)!=0){workerError_=true;stop_=true;}
 Request req;std::shared_ptr<Page> retired;
 auto cancelPreparation=[&](recovery::v2::progress::WorkToken& token,bool known=true){if(!known&&preparationTracking_.ledger){helperDispositionKnown_=false;workerError_=true;preparationTracking_.complete(token,false);return;}if(cancelledPreparationCount_==cancelledPreparations_.size()){workerError_=true;preparationTracking_.complete(token,false);return;}cancelledPreparations_[cancelledPreparationCount_++]=token;token={};};
 while(!stop_){while(retired_.pop(retired))retired.reset();if(!requests_.pop(req)){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
  if(req.resume){bool saved=true;try{writeViewResume(data_,req.page,total_,generation_,req.clean,viewSet_);}catch(...){saved=false;workerError_=true;}if(!resumeTracking_.complete(req.tracking,saved))workerError_=true;continue;}
  /* Queued prefetch yields to a foreground demand; an already running helper is never killed or misreported. */
  const int foreground=foregroundPage_.load();if((foreground>=0&&req.page!=foreground)||(foreground<0&&std::abs(req.page-prefetchCenter_.load())>1)){Result skipped;skipped.page=req.page;skipped.skipped=true;skipped.tracking=req.tracking;req.tracking={};++backgroundYields_;recovery::v2::e2::mark("reader:prefetch-yield",unsigned(skipped.page));bool delivered=false;while(!stop_&&!(delivered=results_.push(std::move(skipped))))std::this_thread::sleep_for(std::chrono::milliseconds(2));if(!delivered)cancelPreparation(skipped.tracking);continue;}
  while(!stop_&&(writing_||resident_->load()>=4)){while(retired_.pop(retired))retired.reset();std::this_thread::sleep_for(std::chrono::milliseconds(2));}if(stop_){cancelPreparation(req.tracking);break;}
  preparing_=true;Result out;out.page=req.page;out.tracking=req.tracking;req.tracking={};bool reaped=false;try{auto prepared=prepareViewPage(application_,data_,req.page,stop_,viewSet_);reaped=true;out.value=makePage(std::move(prepared));}catch(const std::exception& e){out.failed=true;out.reconciled=reaped||lifecycle::reconciledHelperFailure(e);}catch(...){out.failed=true;out.reconciled=reaped;}if(!out.reconciled&&preparationTracking_.ledger){helperDispositionKnown_=false;workerError_=true;}
  preparing_=false;bool delivered=false;while(!stop_&&!(delivered=results_.push(std::move(out))))std::this_thread::sleep_for(std::chrono::milliseconds(2));
  if(!delivered){out.value.reset();cancelPreparation(out.tracking,out.reconciled);}
 }
 // Terminal queued preparation has no live child. Earlier queued positions
 // are superseded only by the final durable position, never by a stop flag.
 while(requests_.pop(req)){if(!req.resume){cancelPreparation(req.tracking);continue;}if(deferredResumeCount_==deferredResumes_.size()){workerError_=true;resumeTracking_.complete(req.tracking,false);}else{deferredResumes_[deferredResumeCount_++]=req.tracking;req.tracking={};}}
 while(retired_.pop(retired))retired.reset();closePageServer(); /* the persistent page server never outlives preparation */preparationStopped_.store(true,std::memory_order_release);while(!flushSignalled_)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 bool saved=true;try{writeViewResume(data_,closingPage_,total_,generation_,cleanClose_&&!workerError_,viewSet_);}catch(...){saved=false;workerError_=true;}
 for(unsigned n=0;n<deferredResumeCount_;++n)if(!resumeTracking_.complete(deferredResumes_[n],saved))workerError_=true;
 if(!resumeTracking_.complete(finalResumeTracking_,saved))workerError_=true;done_=true;
}
unsigned Reader::unsaved()const{unsigned n=0;for(auto& c:cache_)if(c)n+=c->saves->unsaved();return n;}
unsigned Reader::committed()const{unsigned n=retiredCommits_;for(auto& c:cache_)if(c)n+=c->saves->committed;return n;}
void Reader::beginQuiesce()noexcept{
 if(quiescing_)return;quiescing_=true;wanted_=-1;navRefusal_=NavigationRefusal::None;interrupted_=contact();exit_=true;state_=State::Closing;closingPage_=activatedPage_;cancel();stop_=true;
 // Existing zero-time finish signals stop and joins only an already-done worker.
 // Keep the qualified Pipeline implementation unchanged.
 for(auto& c:cache_)if(c)(void)c->saves->finish(0);
}
bool Reader::pollQuiesce(bool canonicalDrained,bool canonicalClean)noexcept{
 beginQuiesce();bool pagesDrained=true;
 for(auto& c:cache_)if(c){bool done=c->saves->finish(0);pagesDrained=done&&pagesDrained;if(c->saves->failed())workerError_=true;}
 // Prefetch results own live save workers even if normal tick never installed
 // them. Reconcile those owners before claiming that the reader is drained.
 const bool preparationStopped=preparationStopped_.load(std::memory_order_acquire);
 if(preparationStopped)for(unsigned n=0;n<cancelledPreparationCount_;++n)if(!preparationTracking_.complete(cancelledPreparations_[n],true))workerError_=true;
 bool resultsEmpty=false;
 for(unsigned n=0;n<9;++n){
  auto slot=std::find_if(closingPages_.begin(),closingPages_.end(),[](const auto& p){return !p;});
  if(slot==closingPages_.end())break;
  Result result;if(!results_.pop(result)){resultsEmpty=true;break;}
  if(result.value)*slot=std::move(result.value);
  reconcile(result);
 }
 for(auto& p:closingPages_)if(p){
  bool done=p->saves->finish(0);pagesDrained=done&&pagesDrained;
  if(p->saves->failed()||p->saves->unsaved()){
   if(done)workerError_=true;
  }
  if(done){retiredCommits_+=p->saves->committed.load();p.reset();}
 }
 pagesDrained=pagesDrained&&resultsEmpty;
 if(!flushSignalled_.load(std::memory_order_acquire)&&canonicalDrained&&pagesDrained&&preparationStopped&&helperDispositionKnown_.load(std::memory_order_acquire)){
  if(!resumeTracking_.accept(finalResumeTracking_))workerError_=true;
  cleanClose_=canonicalClean&&!interrupted_&&!workerError_&&unsaved()==0;
  flushSignalled_.store(true,std::memory_order_release);
 }
 if(done_.load(std::memory_order_acquire)&&preparer_.joinable()){preparer_.join();state_=State::Closed;}
 return pagesDrained&&!preparer_.joinable();
}
bool Reader::finish(unsigned ms,bool canonicalClean)noexcept{
 beginQuiesce();const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
 do{if(pollQuiesce(true,canonicalClean))return true;if(std::chrono::steady_clock::now()>=end)return false;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(true);
}
}
