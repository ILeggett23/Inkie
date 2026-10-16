// SPDX-License-Identifier: AGPL-3.0-only
#include "power_events.h"
#include <string>
#include <systemd/sd-bus.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <time.h>
#include <unistd.h>
namespace recovery::power {namespace {
constexpr const char* Service="org.freedesktop.login1";
constexpr const char* Path="/org/freedesktop/login1";
constexpr const char* Interface="org.freedesktop.login1.Manager";
constexpr const char* Driver="org.freedesktop.DBus";
constexpr const char* DriverPath="/org/freedesktop/DBus";
constexpr std::uint64_t MaximumStartupUsec=500000,MaximumDelayUsec=5000000,PollUsec=5000,ClockSlackUsec=5000;
void need(bool value){if(!value)throw std::runtime_error("power-bus-protocol");}
void checked(int result){need(result>=0);}
ClockReading clocks()noexcept{timespec mono{},boot{};if(clock_gettime(CLOCK_MONOTONIC,&mono)||clock_gettime(CLOCK_BOOTTIME,&boot))return {};return {std::uint64_t(mono.tv_sec)*1000000+std::uint64_t(mono.tv_nsec)/1000,std::uint64_t(boot.tv_sec)*1000000+std::uint64_t(boot.tv_nsec)/1000};}
bool sender(sd_bus_message* message,const char* expected){auto actual=sd_bus_message_get_sender(message);return actual&&expected&&!std::strcmp(actual,expected);}
}
struct Events::Impl {
 enum class Call {Owner,Uid,Before,Inhibit,After};
 Config config;mutable std::mutex mutex;Snapshot view;bool started{},sealed{},terminal{},ownerMatched{},sleepMatched{},authenticated{};int inhibitor{-1},wake{-1};
 std::thread worker;std::atomic<bool> done{true};std::atomic<Reason> requested{Reason::None};std::atomic<std::uint64_t> acknowledged{0};
 sd_bus* bus{};sd_bus_slot *ownerSlot{},*sleepSlot{},*requestSlot{};Call call{};std::string owner;std::uint64_t startedBoot{},startedMono{},providerDelay{};ClockReading previous{};
 explicit Impl(Config c):config(std::move(c)){}
 ClockReading now()const noexcept{return config.fixtureClock?config.fixtureClock(config.fixtureClockContext):clocks();}
 void wakeWorker()noexcept{if(wake>=0){std::uint64_t one=1;(void)write(wake,&one,sizeof one);}}
 void sealLocked()noexcept{if(!sealed){sealed=true;++view.generation;}view.platformVerified=false;}
 void release()noexcept{if(inhibitor>=0){close(inhibitor);inhibitor=-1;}std::lock_guard lock(mutex);view.inhibitorHeld=false;}
 void closeGeneration(Reason why)noexcept{{std::lock_guard lock(mutex);sealLocked();if(view.state!=State::Closed){view.state=State::Closed;view.reason=why;}}terminal=true;release();}
 void requestClose(Reason why)noexcept{{std::lock_guard lock(mutex);if(!started||view.state==State::Unsupported||view.state==State::Closed)return;sealLocked();view.state=State::Closed;view.reason=why;}requested=why;wakeWorker();}
 bool validConfig()const noexcept{const auto& r=config.route;if(r.scope==Scope::Unverified||!r.allRelevantSleepRoutes||!r.finitePreSleepBarrier||r.evidenceId.empty()||r.evidenceId.size()>512||r.delayBudgetUsec==0||r.delayBudgetUsec>MaximumDelayUsec||r.maximumSignalLagUsec>=r.delayBudgetUsec||config.startupTimeoutUsec==0||config.startupTimeoutUsec>MaximumStartupUsec)return false;
  if(r.scope==Scope::PrivateFixture)return config.privateBusAddress.starts_with("unix:path=/")&&config.privateBusAddress.find(';')==std::string::npos&&config.privateBusAddress.size()<=1024;
  return r.scope==Scope::VerifiedPlatform&&config.privateBusAddress.empty()&&!config.fixtureClock&&config.expectedServiceUid==0;
 }
 bool start(){if(started)return false;started=true;if(!validConfig()){std::lock_guard lock(mutex);view.reason=config.route.scope==Scope::Unverified?Reason::UnverifiedRoute:Reason::InvalidConfiguration;return false;}wake=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);if(wake<0){view.reason=Reason::BusUnavailable;return false;}view.state=State::Starting;view.reason=Reason::None;done=false;try{worker=std::thread([this]{run();});}catch(...){done=true;close(wake);wake=-1;view.state=State::Closed;view.reason=Reason::BusUnavailable;return false;}return true;}
 static int reply(sd_bus_message* message,void* data,sd_bus_error*)noexcept{auto* self=static_cast<Impl*>(data);try{self->handleReply(message);}catch(...){self->closeGeneration(Reason::ProtocolError);}return 1;}
 static int installed(sd_bus_message* message,void* data,sd_bus_error*)noexcept{auto* self=static_cast<Impl*>(data);try{if(self->terminal)return 1;need(!sd_bus_message_is_method_error(message,nullptr)&&sender(message,Driver));if(!self->ownerMatched){self->ownerMatched=true;self->begin(Call::Owner);}else{self->sleepMatched=true;self->begin(Call::Before);}}catch(...){self->closeGeneration(Reason::ProtocolError);}return 1;}
 static int ownerChanged(sd_bus_message* message,void* data,sd_bus_error*)noexcept{auto* self=static_cast<Impl*>(data);try{if(self->terminal||!sender(message,Driver))return 1;const char *name{},*old{},*next{};need(sd_bus_message_read(message,"sss",&name,&old,&next)>0&&sd_bus_message_at_end(message,1)>0);if(name&&!std::strcmp(name,Service))self->closeGeneration(Reason::OwnerChanged);}catch(...){self->closeGeneration(Reason::ProtocolError);}return 1;}
 static int sleep(sd_bus_message* message,void* data,sd_bus_error*)noexcept{auto* self=static_cast<Impl*>(data);try{self->handleSleep(message);}catch(...){self->closeGeneration(Reason::ProtocolError);}return 1;}
 void begin(Call next){if(terminal)return;requestSlot=sd_bus_slot_unref(requestSlot);call=next;auto n=now();need(n.boottimeUsec>=startedBoot&&n.boottimeUsec-startedBoot<config.startupTimeoutUsec);checked(sd_bus_set_method_call_timeout(bus,config.startupTimeoutUsec-(n.boottimeUsec-startedBoot)));
  if(next==Call::Owner)checked(sd_bus_call_method_async(bus,&requestSlot,Driver,DriverPath,Driver,"GetNameOwner",reply,this,"s",Service));
  else if(next==Call::Uid)checked(sd_bus_call_method_async(bus,&requestSlot,Driver,DriverPath,Driver,"GetConnectionUnixUser",reply,this,"s",owner.c_str()));
  else if(next==Call::Inhibit)checked(sd_bus_call_method_async(bus,&requestSlot,owner.c_str(),Path,Interface,"Inhibit",reply,this,"ssss","sleep","Inkbound reader","Bounded accepted-work quiescence before sleep","delay"));
  else checked(sd_bus_call_method_async(bus,&requestSlot,owner.c_str(),Path,"org.freedesktop.DBus.Properties","GetAll",reply,this,"s",Interface));
 }
 std::pair<bool,std::uint64_t> properties(sd_bus_message* message){checked(sd_bus_message_enter_container(message,'a',"{sv}"));bool foundPreparing=false,foundDelay=false,preparing=false;std::uint64_t delay{};unsigned count=0;
  for(;;){int entry=sd_bus_message_enter_container(message,'e',"sv");checked(entry);if(!entry)break;need(++count<=128);const char* name{};need(sd_bus_message_read(message,"s",&name)>0&&name);if(!std::strcmp(name,"PreparingForSleep")){need(!foundPreparing);foundPreparing=true;checked(sd_bus_message_enter_container(message,'v',"b"));int value{};need(sd_bus_message_read(message,"b",&value)>0);preparing=value;checked(sd_bus_message_exit_container(message));}else if(!std::strcmp(name,"InhibitDelayMaxUSec")){need(!foundDelay);foundDelay=true;checked(sd_bus_message_enter_container(message,'v',"t"));need(sd_bus_message_read(message,"t",&delay)>0);checked(sd_bus_message_exit_container(message));}else checked(sd_bus_message_skip(message,"v"));need(sd_bus_message_at_end(message,0)>0);checked(sd_bus_message_exit_container(message));}
  checked(sd_bus_message_exit_container(message));need(sd_bus_message_at_end(message,1)>0&&foundPreparing&&foundDelay&&delay>0&&delay<std::numeric_limits<std::uint64_t>::max()/2);need(std::min(delay,config.route.delayBudgetUsec)>config.route.maximumSignalLagUsec);return {preparing,delay};
 }
 bool inhibitorLive()noexcept{if(inhibitor<0)return false;struct stat st{};int flags=fcntl(inhibitor,F_GETFL);if(fstat(inhibitor,&st)||!S_ISFIFO(st.st_mode)||flags<0||(flags&O_ACCMODE)!=O_WRONLY)return false;pollfd p{inhibitor,0,0};int result=poll(&p,1,0);return result>=0&&!(p.revents&(POLLERR|POLLHUP|POLLNVAL));}
 void handleReply(sd_bus_message* message){if(terminal)return;if(sd_bus_message_is_method_error(message,nullptr)){closeGeneration(call==Call::Inhibit?Reason::InhibitorDenied:call==Call::Owner?Reason::ServiceUnavailable:Reason::ProtocolError);return;}
  need(sender(message,(call==Call::Owner||call==Call::Uid)?Driver:owner.c_str()));
  if(call==Call::Owner){const char* value{};need(sd_bus_message_read(message,"s",&value)>0&&value&&value[0]==':'&&std::strlen(value)<=255&&sd_bus_message_at_end(message,1)>0);owner=value;{std::lock_guard lock(mutex);view.uniqueOwner=owner;}begin(Call::Uid);return;}
  if(call==Call::Uid){std::uint32_t uid{};need(sd_bus_message_read(message,"u",&uid)>0&&sd_bus_message_at_end(message,1)>0);if(uid!=config.expectedServiceUid){closeGeneration(Reason::UnauthenticatedService);return;}authenticated=true;checked(sd_bus_match_signal_async(bus,&sleepSlot,nullptr,Path,Interface,"PrepareForSleep",sleep,installed,this));return;}
  if(call==Call::Inhibit){int borrowed=-1;need(sd_bus_message_read(message,"h",&borrowed)>0&&borrowed>=0&&sd_bus_message_at_end(message,1)>0);need(inhibitor<0);inhibitor=fcntl(borrowed,F_DUPFD_CLOEXEC,3);need(inhibitor>=0);if(!inhibitorLive()){closeGeneration(Reason::InhibitorLost);return;}{std::lock_guard lock(mutex);view.inhibitorHeld=true;++view.acquisitions;}begin(Call::After);return;}
  auto [preparing,delay]=properties(message);if(preparing){closeGeneration(call==Call::Before?Reason::InitiallyPreparing:Reason::StartupRace);return;}
  if(call==Call::Before){providerDelay=delay;{std::lock_guard lock(mutex);view.providerDelayUsec=delay;}begin(Call::Inhibit);return;}
  need(call==Call::After&&ownerMatched&&sleepMatched&&authenticated&&inhibitor>=0);if(!inhibitorLive()){closeGeneration(Reason::InhibitorLost);return;}if(delay!=providerDelay){closeGeneration(Reason::StartupRace);return;}{std::lock_guard lock(mutex);if(sealed)return;view.state=config.route.scope==Scope::VerifiedPlatform?State::Ready:State::FixtureReady;view.platformVerified=config.route.scope==Scope::VerifiedPlatform;view.reason=Reason::None;}
 }
 void handleSleep(sd_bus_message* message){if(terminal)return;if(!authenticated||!sender(message,owner.c_str())){std::lock_guard lock(mutex);++view.foreignSignals;return;}int active{};need(sd_bus_message_read(message,"b",&active)>0&&sd_bus_message_at_end(message,1)>0);State state;{std::lock_guard lock(mutex);state=view.state;}if(state==State::Starting){closeGeneration(Reason::StartupRace);return;}if(!active){closeGeneration(Reason::Resumed);return;}if(state==State::Preparing){std::lock_guard lock(mutex);++view.duplicateSignals;return;}if(state!=State::Ready&&state!=State::FixtureReady)return;
  auto time=now();need(time.boottimeUsec&&time.monotonicUsec);std::uint64_t receipt=time.boottimeUsec,stamp{};if(sd_bus_message_get_monotonic_usec(message,&stamp)>=0){need(stamp<=time.monotonicUsec&&time.monotonicUsec-stamp<=time.boottimeUsec);receipt=std::min(receipt,time.boottimeUsec-(time.monotonicUsec-stamp));}
  auto allowance=std::min(providerDelay,config.route.delayBudgetUsec)-config.route.maximumSignalLagUsec;need(receipt<=std::numeric_limits<std::uint64_t>::max()-allowance);{std::lock_guard lock(mutex);sealLocked();view.state=State::Preparing;view.reason=Reason::SleepPreparing;view.deadlineBoottimeUsec=receipt+allowance;}
 }
 bool clockValid(ClockReading n){if(!n.monotonicUsec||!n.boottimeUsec||n.boottimeUsec<n.monotonicUsec)return false;if(previous.monotonicUsec){if(n.monotonicUsec<previous.monotonicUsec||n.boottimeUsec<previous.boottimeUsec)return false;auto a=n.boottimeUsec-n.monotonicUsec,b=previous.boottimeUsec-previous.monotonicUsec;if(a>b?a-b>ClockSlackUsec:b-a>ClockSlackUsec)return false;}previous=n;return true;}
 void run()noexcept{try{previous=now();need(clockValid(previous));startedBoot=previous.boottimeUsec;startedMono=previous.monotonicUsec;checked(sd_bus_new(&bus));auto address=config.route.scope==Scope::PrivateFixture?config.privateBusAddress:"unix:path=/run/dbus/system_bus_socket";checked(sd_bus_set_address(bus,address.c_str()));checked(sd_bus_set_bus_client(bus,1));checked(sd_bus_negotiate_fds(bus,1));checked(sd_bus_negotiate_timestamp(bus,1));checked(sd_bus_set_method_call_timeout(bus,config.startupTimeoutUsec));checked(sd_bus_start(bus));bool subscribed=false;
  while(!terminal){auto command=requested.load();if(command!=Reason::None){closeGeneration(command);break;}auto time=now();if(!clockValid(time)){closeGeneration(Reason::ClockDiscontinuity);break;}Snapshot state;{std::lock_guard lock(mutex);state=view;}
   if(state.state==State::Starting&&(time.boottimeUsec-startedBoot>=config.startupTimeoutUsec||time.monotonicUsec-startedMono>=config.startupTimeoutUsec)){closeGeneration(Reason::StartupTimeout);break;}
   if(state.state==State::Preparing){if(time.boottimeUsec>=state.deadlineBoottimeUsec){closeGeneration(Reason::DelayExpired);break;}if(acknowledged.load()==state.generation){closeGeneration(Reason::Quiesced);break;}}
   if(!subscribed&&sd_bus_is_ready(bus)>0){ucred peer{};socklen_t size=sizeof peer;need(getsockopt(sd_bus_get_fd(bus),SOL_SOCKET,SO_PEERCRED,&peer,&size)==0&&size==sizeof peer&&peer.uid==(config.route.scope==Scope::PrivateFixture?getuid():0));/* Watch only the power service name: an unfiltered watch received every bus client's connect/disconnect, and a burst (>64) closed the generation as ProcessingCapacity. */const std::string rule=std::string("type='signal',sender='")+Driver+"',path='"+DriverPath+"',interface='"+Driver+"',member='NameOwnerChanged',arg0='"+Service+"'";checked(sd_bus_add_match_async(bus,&ownerSlot,rule.c_str(),ownerChanged,installed,this));subscribed=true;}
   unsigned processed=0;for(;processed<64&&!terminal;++processed){int result=sd_bus_process(bus,nullptr);if(result<0){closeGeneration(Reason::ConnectionLost);break;}if(!result)break;}if(terminal)break;if(processed==64){closeGeneration(Reason::ProcessingCapacity);break;}
   auto remaining=PollUsec;{std::lock_guard lock(mutex);auto t=now().boottimeUsec;if(view.state==State::Preparing)remaining=std::min(remaining,view.deadlineBoottimeUsec>t?view.deadlineBoottimeUsec-t:0);else if(view.state==State::Starting)remaining=std::min(remaining,startedBoot+config.startupTimeoutUsec>t?startedBoot+config.startupTimeoutUsec-t:0);}
   int fd=sd_bus_get_fd(bus),events=sd_bus_get_events(bus);if(fd<0||events<0){closeGeneration(Reason::ConnectionLost);break;}pollfd descriptors[3]{{fd,short(events),0},{wake,POLLIN,0},{inhibitor,0,0}};int ready=poll(descriptors,3,int((remaining+999)/1000));if(ready<0&&errno!=EINTR){closeGeneration(Reason::ConnectionLost);break;}if(descriptors[1].revents&POLLIN){std::uint64_t value;while(read(wake,&value,sizeof value)>0){}}if(descriptors[0].revents&(POLLERR|POLLHUP|POLLNVAL)){closeGeneration(Reason::ConnectionLost);break;}if(descriptors[2].revents&(POLLERR|POLLHUP|POLLNVAL)){closeGeneration(Reason::InhibitorLost);break;}
  }
 }catch(...){closeGeneration(Reason::ProtocolError);}release();requestSlot=sd_bus_slot_unref(requestSlot);sleepSlot=sd_bus_slot_unref(sleepSlot);ownerSlot=sd_bus_slot_unref(ownerSlot);bus=sd_bus_close_unref(bus);done=true;}
 Snapshot snapshot()const{std::lock_guard lock(mutex);return view;}
 bool acknowledge(std::uint64_t generation)noexcept{std::lock_guard lock(mutex);if(view.state!=State::Preparing||view.generation!=generation||now().boottimeUsec>=view.deadlineBoottimeUsec)return false;acknowledged=generation;wakeWorker();return true;}
 bool finish(unsigned ms)noexcept{if(!worker.joinable())return true;requestClose(Reason::Stopped);auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);while(!done&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));if(!done)return false;worker.join();if(wake>=0){close(wake);wake=-1;}return true;}
};
Events::Events(Config c):impl_(std::make_unique<Impl>(std::move(c))){}
Events::~Events(){if(!finish(100))std::_Exit(78);}
bool Events::start(){return impl_->start();}Snapshot Events::snapshot()const{return impl_->snapshot();}
bool Events::acknowledgeQuiesced(std::uint64_t generation)noexcept{return impl_->acknowledge(generation);}
void Events::invalidate()noexcept{impl_->requestClose(Reason::ExternalInvalidation);}
bool Events::finish(unsigned ms)noexcept{return impl_->finish(ms);}
}
