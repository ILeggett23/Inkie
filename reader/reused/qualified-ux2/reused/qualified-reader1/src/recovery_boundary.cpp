// SPDX-License-Identifier: AGPL-3.0-only
#include "session.h"
#include "contract.h"
#include "bus.h"
#include "session_limits.h"
#include <atomic>
#include <thread>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <spawn.h>
#include <filesystem>
#include <cstdlib>
extern char **environ;
namespace spike {
namespace {
class OwnedLease final:public RecoveryLease {
 int fd_;recovery::Ns end_;std::atomic<bool> live_{true};std::thread observer_;
public:
 OwnedLease(int fd,recovery::Ns end):fd_(fd),end_(end),observer_([this]{
  while(live_.load(std::memory_order_acquire)){
   pollfd p{fd_,POLLIN,0};int n=poll(&p,1,20);if(n<0){if(errno==EINTR)continue;live_=false;break;}
   if(n>0){ // No renewal messages exist; EOF, stop or unexpected data closes admission.
    live_.store(false,std::memory_order_release);break;
   }
  }
 }){}
 bool valid()const noexcept override{return live_.load(std::memory_order_acquire);}
 Ns deadline()const noexcept override{return end_;}
 bool stopRequested()const noexcept override{return !valid();}
 ~OwnedLease(){live_=false;shutdown(fd_,SHUT_RDWR);if(observer_.joinable())observer_.join();close(fd_);}
};
}
std::unique_ptr<RecoveryLease> receiveQualifiedRecoveryLease()noexcept {
 int fd=-1;try{
 const char*s=getenv("INKBOUND_RECOVERY_SESSION"),*inv=getenv("INVOCATION_ID");
 if(getuid()!=0||!s||!inv||!recovery::hex32(s)||!recovery::hex32(inv))return {};
 fd=recovery::connectOwner(s,2500);if(fd<0)return {};
 auto owner=recovery::peer(fd);if(owner<2){close(fd);return {};}
 recovery::Bus validation;
 auto ownerState=validation.state(recovery::ownerUnit(s));
 if(ownerState.main!=unsigned(owner)||ownerState.active!="active"||ownerState.sub!="running"){close(fd);return {};}
 recovery::Packet hello;hello.kind=recovery::Kind::Child;recovery::copy(hello.binding.session,sizeof hello.binding.session,s);recovery::copy(hello.binding.invocation,sizeof hello.binding.invocation,inv);
 recovery::Packet p;
 if(!recovery::sendPacket(fd,hello)||!recovery::receivePacket(fd,p,5000)||p.kind!=recovery::Kind::Permit||!recovery::validBinding(p.binding)||std::string(p.binding.session)!=s||std::string(p.binding.invocation)!=inv||p.binding.boot!=recovery::boot()||p.binding.mainPid!=unsigned(getpid())||p.binding.ownerPid!=unsigned(owner)||std::string(p.binding.ownerInvocation)!=ownerState.invocation){close(fd);return {};}
 auto t=recovery::now();if(!recovery::limits::validDeadline(t,p.binding.deadline)||!recovery::connected(fd)){close(fd);return {};}
 return std::make_unique<OwnedLease>(fd,p.binding.deadline);
 }catch(...){if(fd>=0)close(fd);return {};}
}
int armOwnedSession()noexcept{
 try{std::string helper=std::filesystem::path(recovery::executable()).parent_path().string()+
#ifdef OWNER_VM_TEST
 "/inkbound-recovery-owner-vm";
#else
 "/inkbound-recovery-owner";
#endif
struct stat st{};if(lstat(helper.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&0022))return 78;
 char*args[]={helper.data(),const_cast<char*>("--launch"),
#ifdef OWNER_VM_TEST
 const_cast<char*>(getenv("OWNER_TEST_LAUNCH_CASE")?getenv("OWNER_TEST_LAUNCH_CASE"):"normal"),
#endif
 nullptr};pid_t pid{};if(posix_spawn(&pid,helper.c_str(),nullptr,nullptr,args,environ))return 78;int status=0;while(waitpid(pid,&status,0)<0){if(errno!=EINTR)return 78;}return WIFEXITED(status)?WEXITSTATUS(status):78;
 }catch(...){return 78;}
}
}
