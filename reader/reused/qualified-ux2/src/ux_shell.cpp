#include "ux_shell.h"
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
namespace ux {
bool catalogueValid(const std::filesystem::path& app)noexcept{try{return app.is_absolute()&&underlay::digest(underlay::readBounded(app/"fixtures/inkbound-device-test.pdf",1024*1024)).toHex()==underlay::FixtureHash;}catch(...){return false;}}
Shell::Shell(const std::filesystem::path& app,const std::filesystem::path& data):app_(app),data_(data),clean_(underlay::Pixels,0xffffffffu),chrome_(underlay::Pixels,0xffffffffu){picker({chrome_,underlay::Width,underlay::Height,underlay::Stride});}
Shell::~Shell(){if(!finish())std::_Exit(78);}
bool Shell::choose(unsigned index){if(index||state_!=State::Picker||opener_.joinable()||!catalogueValid(app_)){++rejected_;return false;}
 state_=State::Loading;pickerRow({chrome_,underlay::Width,underlay::Height,underlay::Stride},true);change_.regions|=8;
 opener_=std::thread([this]{try{if(setpriority(PRIO_PROCESS,static_cast<id_t>(syscall(SYS_gettid)),19))throw std::runtime_error("opener-priority");underlay::privateDirectory(data_);auto saved=reader::readResume(data_,2);prepared_=std::make_unique<underlay::Prepared>(reader::preparePage(app_,data_,saved.page,stop_));if(prepared_->metadata["pages"].toInt()!=2)throw std::runtime_error("catalogue-page-count");}catch(...){openFailed_=true;}done_.store(true,std::memory_order_release);});return true;
}
void Shell::pageBackgrounds(){loadingShown_=false;auto& p=document_->active().prepared;std::copy(p.background.begin(),p.background.end(),clean_.begin());
 // Preserve the qualified PageId/fit/ink geometry. Only its legacy UI footer,
 // outside the PDF's 2060-pixel content fit, is masked in presentation storage.
 fill({clean_,underlay::Width,underlay::Height,underlay::Stride},{0,2060,1620,2160});std::copy(clean_.begin(),clean_.end(),chrome_.begin());
 std::snprintf(counter_.data(),counter_.size(),"%d / %d",document_->page()+1,document_->total());overlay({chrome_,underlay::Width,underlay::Height,underlay::Stride},counter_.data());
}
bool Shell::action(Action a,bool markerContact){if(a==Action::None)return false;if(markerContact||(document_&&document_->contact())){++rejected_;return false;}
 if(a==Action::Exit){if(document_&&!document_->request(reader::Action::Exit)){++rejected_;return false;}state_=State::ExitPending;stop_=true;return true;}
 if(a==Action::Open)return choose(0);
 if(a==Action::Show&&state_==State::Clean){state_=State::Chrome;change_.regions|=7;return true;}
 if(a==Action::Hide&&state_==State::Chrome){state_=State::Clean;change_.regions|=7;return true;}
 if((a==Action::Previous||a==Action::Next)&&state_==State::Chrome&&document_){if(!document_->request(a==Action::Next?reader::Action::Next:reader::Action::Previous)){++rejected_;return false;}state_=State::NavigationPending;return true;}
 ++rejected_;return false;
}
bool Shell::tick(){if(document_&&document_->contact())return false;if(exiting())return false;
 if(state_==State::Loading&&done_.load(std::memory_order_acquire)){opener_.join();if(openFailed_||!prepared_){fail();return false;}document_=std::make_unique<reader::Reader>(app_,data_,std::move(*prepared_));prepared_.reset();pageBackgrounds();state_=State::Clean;change_={true,0};return true;}
 if(document_){bool changed=document_->tick();if(document_->failed()||document_->exiting()){fail();return false;}if(changed){pageBackgrounds();state_=State::Clean;change_={true,0};return true;}
  if(state_==State::NavigationPending&&document_->state()==reader::State::Active){pageBackgrounds();state_=State::Clean;change_.regions|=7;}
  else if(state_==State::NavigationPending&&!loadingShown_){overlay({chrome_,underlay::Width,underlay::Height,underlay::Stride},counter_.data(),true);change_.regions|=1;loadingShown_=true;}
 }return false;
}
void Shell::installed(){if(document_){document_->installed();document_->activate();}}
bool Shell::reserve()noexcept{return writable()&&document_->reserve();}
bool Shell::append(const spike::Point& p)noexcept{return document_&&document_->append(p);}
void Shell::cancel()noexcept{if(document_)document_->cancel();}
bool Shell::finish(unsigned ms)noexcept{stop_=true;auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);cancel();if(opener_.joinable()){while(!done_.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));if(!done_)return false;opener_.join();}
 if(document_){auto left=std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now()).count();return document_->finish(unsigned(std::max<std::int64_t>(0,left)));}return true;}
}
