// SPDX-License-Identifier: AGPL-3.0-only
#include "session_power_v2.h"
#include "e2_provisional_v2.h"
#include <linux/input.h>
#include <sys/ioctl.h>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#include <cctype>
#include <algorithm>
#include <time.h>
namespace recovery::v2::sessionpower {
namespace {
bool safeName(const std::string& n){if(n.empty()||n.size()>64)return false;for(char c:n)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;return true;}
}
WakeLock::WakeLock(std::filesystem::path d,std::string n)noexcept:directory_(std::move(d)),name_(std::move(n)){if(!safeName(name_))failed_=true;}
WakeLock::~WakeLock(){release();}
bool WakeLock::write(const char* file,const std::string& text)noexcept{
 // Existing kernel attribute only: never create, follow a link or truncate.
 int fd=::open((directory_/file).c_str(),O_WRONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return false;
 ssize_t n;do{n=::write(fd,text.data(),text.size());}while(n<0&&errno==EINTR);
 const bool ok=n==ssize_t(text.size());::close(fd);return ok;
}
bool WakeLock::hold(Ns timeout)noexcept{
 if(failed_||!timeout)return false;
 try{if(!write("wake_lock",name_+" "+std::to_string(timeout))){failed_=true;return false;}}catch(...){failed_=true;return false;}
 held_=true;return true;
}
bool WakeLock::release()noexcept{
 if(!held_)return true;held_=false;
 try{return write("wake_unlock",name_);}catch(...){return false;}
}
std::filesystem::path findInput(const std::filesystem::path& sysfs,const std::filesystem::path& dev,const std::string& name)noexcept{
 try{
  if(name.empty())return {};
  std::filesystem::path found;unsigned matches=0;
  for(const auto& e:std::filesystem::directory_iterator(sysfs/"class/input")){
   const auto base=e.path().filename().string();if(base.rfind("event",0)!=0)continue;
   std::ifstream in(e.path()/"device/name");std::string line;if(!std::getline(in,line))continue;
   if(line==name){found=dev/base;++matches;}
  }
  return matches==1?found:std::filesystem::path{}; // ambiguity refuses
 }catch(...){return {};}
}
EndSignals::EndSignals(const std::filesystem::path& p,const std::filesystem::path& c,Ns idle,Ns now)noexcept:idle_(idle),last_(now){
 if(!p.empty())power_=::open(p.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
 if(!c.empty())cover_=::open(c.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
 if(!c.empty()&&cover_<0){if(power_>=0)::close(power_);power_=-1;} // a configured cover must open
}
EndSignals::~EndSignals(){if(power_>=0)::close(power_);if(cover_>=0)::close(cover_);}
bool EndSignals::drain(int fd,bool cover)noexcept{
 input_event e{};
 for(unsigned n=0;n<256;++n){
  auto r=::read(fd,&e,sizeof e);
  if(r<0){if(errno==EAGAIN||errno==EWOULDBLOCK)return false;if(errno==EINTR)continue;return true;} // lost device ends the session
  if(r==0)return true;if(r!=ssize_t(sizeof e))return true;
  if(!cover&&e.type==EV_KEY&&e.code==KEY_POWER&&e.value==1)return true;
  if(cover&&e.type==EV_SW&&e.code==SW_LID&&e.value==1)return true;
 }
 return false;
}
bool EndSignals::endRequested(Ns now)noexcept{
 if(requested_)return true;
 if(!valid()){requested_=true;return true;}
 if(drain(power_,false)||(cover_>=0&&drain(cover_,true))||(now>last_&&now-last_>=idle_))requested_=true;
 return requested_;
}
Ns suspendOffset(void*)noexcept{
 timespec m{},b{};if(clock_gettime(CLOCK_MONOTONIC,&m)||clock_gettime(CLOCK_BOOTTIME,&b))return 0;
 const Ns mono=Ns(m.tv_sec)*1000000000ull+Ns(m.tv_nsec),boot=Ns(b.tv_sec)*1000000000ull+Ns(b.tv_nsec);return boot>mono?boot-mono:0;
}
bool listedWakeLock(std::string_view contents,std::string_view name)noexcept{
 if(name.empty())return false;std::size_t i=0;
 while(i<contents.size()){while(i<contents.size()&&std::isspace(static_cast<unsigned char>(contents[i])))++i;std::size_t j=i;while(j<contents.size()&&!std::isspace(static_cast<unsigned char>(contents[j])))++j;
  if(j>i&&contents.substr(i,j-i)==name)return true;i=j;}
 return false;
}
bool wakeLockActive(const std::filesystem::path& dir,std::string_view name)noexcept{
 try{std::ifstream in(dir/"wake_lock");if(!in)return true;std::string text(4096,'\0');in.read(text.data(),std::streamsize(text.size()));text.resize(std::size_t(in.gcount()));return listedWakeLock(text,name);}catch(...){return true;}
}
EndSignals::Press EndSignals::takePress()noexcept{
 if(power_<0)return Press::Lost;input_event e{};bool pressed=false;
 for(unsigned n=0;n<256;++n){auto r=::read(power_,&e,sizeof e);
  if(r<0){if(errno==EAGAIN||errno==EWOULDBLOCK)break;if(errno==EINTR)continue;return Press::Lost;}
  if(r!=ssize_t(sizeof e))return Press::Lost;if(e.type==EV_KEY&&e.code==KEY_POWER&&e.value==1)pressed=true;}
 return pressed?Press::Pressed:Press::None;
}
bool OwnerPresses::resumed(Ns now)const noexcept{return lastResume_&&now>=lastResume_&&now-lastResume_<SessionPower::WakeGrace;}
OwnerPresses::Event OwnerPresses::step(Ns now,Ns offset,bool pressed,bool lockHeld)noexcept{
 if(offset>offset_&&offset-offset_>=SessionPower::ResumeThreshold)lastResume_=now?now:1;if(offset>offset_)offset_=offset;
 if(!pressedAt_){if(!pressed)return Event::None;pressedAt_=now?now:1;candidate_=!lockHeld||resumed(now);return candidate_?Event::PressedAsleep:Event::Pressed;}
 if(candidate_&&lockHeld){pressedAt_=0;candidate_=false;return Event::WakeConfirmed;}
 return Event::None;
}
bool DisplayRail::shorten(unsigned ms)noexcept{
 try{
  if(!shortened_){std::ifstream in(length_);std::string v;if(!in||!std::getline(in,v))return false;while(!v.empty()&&std::isspace(static_cast<unsigned char>(v.back())))v.pop_back();
   if(v.empty()||v.size()>9||!std::all_of(v.begin(),v.end(),[](char c){return c>='0'&&c<='9';}))return false;saved_=v;}
  const std::string text=std::to_string(ms);int fd=::open(length_.c_str(),O_WRONLY|O_TRUNC|O_CLOEXEC|O_NOFOLLOW); /* as shell redirection does; sysfs ignores the truncation */if(fd<0)return false;
  ssize_t n;do{n=::write(fd,text.data(),text.size());}while(n<0&&errno==EINTR);::close(fd);if(n!=ssize_t(text.size()))return false;
  shortened_=true;return true;
 }catch(...){return false;}
}
bool DisplayRail::restore()noexcept{
 if(!shortened_)return true;
 try{int fd=::open(length_.c_str(),O_WRONLY|O_TRUNC|O_CLOEXEC|O_NOFOLLOW); /* as shell redirection does; sysfs ignores the truncation */if(fd<0)return false;ssize_t n;do{n=::write(fd,saved_.data(),saved_.size());}while(n<0&&errno==EINTR);::close(fd);
  if(n!=ssize_t(saved_.size()))return false;shortened_=false;return true;}catch(...){return false;}
}
int readSwitch(int fd,unsigned code,void*)noexcept{constexpr unsigned Bits=sizeof(unsigned long)*8;std::array<unsigned long,(SW_MAX+Bits)/Bits> bits{};if(fd<0||code>SW_MAX||ioctl(fd,EVIOCGSW(sizeof(bits)),bits.data())<0)return -1;return int((bits[code/Bits]>>(code%Bits))&1ul);}
int readLid(int fd,void*)noexcept{return readSwitch(fd,SW_LID,nullptr);}

SessionPower::SessionPower(std::filesystem::path dir,std::string name,Ns timeout,const std::filesystem::path& key,const std::filesystem::path& cover,Ns now,OffsetSource offset,void* context,LidQuery lidQuery,void* lidContext,SwitchQuery switches,void* switchContext,std::filesystem::path sysRoot)noexcept
 :nativeWake_(std::move(sysRoot)),switchQuery_(switches),switchContext_(switchContext),lock_(std::move(dir),std::move(name)),lidQuery_(lidQuery),lidContext_(lidContext),timeout_(timeout),last_(now),offsetSource_(offset),offsetContext_(context){
 if(!key.empty())power_=::open(key.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
 if(!cover.empty())cover_=::open(cover.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW); /* optional: without it a cover sleep is unavailable */
 if(offsetSource_)offset_=offsetSource_(offsetContext_);refreshCover(now);refreshPen(now,false);
}
SessionPower::~SessionPower(){if(rail_)rail_->restore();if(power_>=0)::close(power_);if(cover_>=0)::close(cover_);}
void SessionPower::configure(SleepConfig c,Ns now)noexcept{
 config_=c;coverWanted_=c.cover&&cover_>=0;if(now>last_)last_=now;refreshCover(now);
 if(coverWanted_&&lidKnown_&&lidClosed_&&state_==PowerState::Awake){state_=PowerState::SleepPending;reason_=SleepReason::Cover;}
 if(state_==PowerState::SleepPending&&((reason_==SleepReason::Cover&&!coverWanted_)||(reason_==SleepReason::Idle&&!config_.idle))){if(rail_&&rail_->shortened()&&!rail_->restore())++railFailures_;state_=PowerState::Awake;reason_=SleepReason::None;}
}
void SessionPower::wakeUp(Ns now)noexcept{if(rail_&&rail_->shortened()){const bool ok=rail_->restore();if(!ok)++railFailures_;e2::mark(ok?"power:display-rail-restored":"power:display-rail-restore-failed",railShortenings_);}state_=PowerState::Awake;reason_=SleepReason::None;last_=now;renewed_=0;wake_=true;++wakes_;}
void SessionPower::coverState(int value,Ns now,bool edge)noexcept{if(value!=0&&value!=1)return;const bool opening=(lidKnown_&&lidClosed_)||reason_==SleepReason::Cover||edge;lidKnown_=true;lidClosed_=value==1;e2::mark("power:cover-state",unsigned(value));if(lidClosed_){if(coverWanted_&&state_==PowerState::Awake){state_=PowerState::SleepPending;reason_=SleepReason::Cover;}}else if(state_!=PowerState::Awake&&opening)wakeUp(now);}
void SessionPower::refreshCover(Ns now)noexcept{if(cover_<0||!lidQuery_)return;++lidQueries_;const int state=lidQuery_(cover_,lidContext_);if(state!=0&&state!=1){++lidQueryFailures_;e2::mark("power:cover-query-failed",lidQueryFailures_);return;}coverState(state,now);}
void SessionPower::readInputs(Ns now)noexcept{
 if(offsetSource_){const Ns off=offsetSource_(offsetContext_);if(off>offset_&&off-offset_>=ResumeThreshold){resumedAt_=now?now:1;resumed_=true;refreshCover(now);refreshPen(now,true);(void)inputWake(nativeWake_.afterResume(),now);}if(off>offset_)offset_=off;}
 input_event e{};
 if(power_>=0)for(unsigned n=0;n<256;++n){auto r=::read(power_,&e,sizeof e);
  if(r<0){if(errno==EAGAIN||errno==EWOULDBLOCK)break;if(errno==EINTR)continue;end_=true;break;} /* a lost power key ends the session */
  if(r!=ssize_t(sizeof e)){end_=true;break;}
  if(e.type!=EV_KEY||e.code!=KEY_POWER||e.value!=1)continue;
  if(state_!=PowerState::Awake)wakeUp(now);
  else if(resumedAt_&&now>=resumedAt_&&now-resumedAt_<WakeGrace)++swallowed_; /* the press that woke the tablet */
  else end_=true;}
 if(cover_>=0)for(unsigned n=0;n<256;++n){auto r=::read(cover_,&e,sizeof e);
  if(r<0){if(errno==EAGAIN||errno==EWOULDBLOCK)break;if(errno==EINTR)continue;::close(cover_);cover_=-1;coverWanted_=false;break;} /* a lost cover only disables cover sleep */
  if(r!=ssize_t(sizeof e)){::close(cover_);cover_=-1;coverWanted_=false;break;}
  if(e.type==EV_SYN&&e.code==SYN_DROPPED){lidDropped_=true;++lidDrops_;e2::mark("power:cover-dropped",lidDrops_);continue;}if(lidDropped_){if(e.type==EV_SYN&&e.code==SYN_REPORT){lidDropped_=false;refreshCover(now);refreshPen(now,true);}continue;}
  if(e.type==EV_SW&&e.code==SW_LID)coverState(e.value,now,true);if(e.type==EV_SW&&e.code==SW_PEN_INSERTED)penState(e.value,now,true);}
 if(config_.idle&&state_==PowerState::Awake&&now>last_&&now-last_>=config_.idle){state_=PowerState::SleepPending;reason_=SleepReason::Idle;}
}
void SessionPower::activity(Ns now)noexcept{
 if(state_==PowerState::Sleeping)return;if(now>last_)last_=now;
 if(state_==PowerState::SleepPending&&reason_==SleepReason::Idle){if(rail_&&rail_->shortened()&&!rail_->restore())++railFailures_;state_=PowerState::Awake;reason_=SleepReason::None;}
}
bool SessionPower::inputWake(WakeCause cause,Ns now)noexcept{if(cause==WakeCause::None||state_!=PowerState::Sleeping||(lidKnown_&&lidClosed_))return false;if(!lock_.hold(timeout_)){e2::mark("power:optional-wake-lock-failed",unsigned(cause));return false;}wakeCause_=cause;wakeUp(now);renewed_=now;resumedAt_=now?now:1;e2::mark("power:input-wake",unsigned(cause));return true;}
void SessionPower::penState(int value,Ns now,bool edge)noexcept{if(value!=0&&value!=1)return;const bool detach=edge&&penKnown_&&penDocked_&&value==0;penKnown_=true;penDocked_=value==1;if(detach)(void)inputWake(WakeCause::PenUndocked,now);}
void SessionPower::refreshPen(Ns now,bool edge)noexcept{if(cover_<0||!switchQuery_)return;int value=switchQuery_(cover_,SW_PEN_INSERTED,switchContext_);if(value<0){penKnown_=false;return;}penState(value,now,edge);}
void SessionPower::prepareSleepDisplay()noexcept{if(state_!=PowerState::SleepPending||!rail_||rail_->shortened())return;const bool ok=rail_->shorten(1000);if(ok)++railShortenings_;else ++railFailures_;e2::mark(ok?"power:display-rail-shortened":"power:display-rail-failed",railShortenings_);}
bool SessionPower::sleep(Ns)noexcept{if(state_!=PowerState::SleepPending)return false;prepareSleepDisplay();nativeWake_.baseline();wakeCause_=WakeCause::None;state_=PowerState::Sleeping;++sleeps_;renewed_=0;return lock_.release();}
bool SessionPower::tick(Ns now,bool admitted)noexcept{
 readInputs(now);
 if(!admitted||!valid())return lock_.held()?lock_.release():true;
 if(state_==PowerState::Sleeping)return true; /* asleep: the lock stays released so autosleep can suspend */
 if(lock_.held()&&renewed_&&now>=renewed_&&now-renewed_<timeout_/4)return true;
 if(!lock_.hold(timeout_))return false;renewed_=now;return true;
}
}
