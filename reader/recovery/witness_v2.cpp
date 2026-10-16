// SPDX-License-Identifier: AGPL-3.0-only
#include "witness_v2.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace recovery::v2::witness {namespace {
struct Fd {int value;~Fd(){if(value>=0)close(value);}};
int directory(const std::string& path){
 Fd current{open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC)};if(current.value<0)return -1;
 std::size_t at=1;while(at<path.size()){
  auto end=path.find('/',at);if(end==std::string::npos)end=path.size();auto part=path.substr(at,end-at);
  if(part.empty()||part=="."||part=="..")return -1;
  int next=openat(current.value,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(next<0)return -1;
  close(current.value);current.value=next;at=end+1;
 }
 int result=current.value;current.value=-1;return result;
}
}
bool authenticated(const SessionIdentity& identity,const Receipt& receipt,const Authentication& auth)noexcept{
 return validIdentity(identity)&&receipt.witnessPid>1&&receipt.drained&&auth.kernelCredentialsVerified&&auth.sameOriginalConnection&&auth.originalConnection&&auth.peerUid==0&&auth.peerPid==receipt.witnessPid&&auth.controlBefore==receipt.witnessPid&&auth.controlAfter==receipt.witnessPid&&auth.invocationBefore==identity.invocation.data()&&auth.invocationAfter==identity.invocation.data();
}
bool membership(const SessionIdentity& identity,pid_t self)noexcept{
 try{
  if(!validIdentity(identity)||self!=getpid()||recovery::boot()!=identity.boot.data()||recovery::managerMount()!=identity.mount.data())return false;
  const std::string group=identity.group.data();if(recovery::readSmall("/proc/self/cgroup").find("0::"+group+"\n")==std::string::npos)return false;
  Fd fd{directory(std::string(identity.mount.data())+group)};if(fd.value<0)return false;struct stat st{};
  return !fstat(fd.value,&st)&&st.st_dev==identity.device&&st.st_ino==identity.inode&&recovery::inspectSubtree(fd.value,self,identity.device);
 }catch(...){return false;}
}
bool absent(const SessionIdentity& identity)noexcept{
 try{
  if(!validIdentity(identity)||recovery::boot()!=identity.boot.data()||recovery::managerMount()!=identity.mount.data())return false;
  // Hold a verified no-symlink parent descriptor across the final absence read.
  const std::string path=std::string(identity.mount.data())+identity.group.data();const auto slash=path.rfind('/');
  Fd parent{directory(path.substr(0,slash))};if(parent.value<0)return false;struct stat st{},parentStat{};if(fstat(parent.value,&parentStat)||parentStat.st_dev!=identity.device)return false;errno=0;
  return fstatat(parent.value,path.substr(slash+1).c_str(),&st,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT;
 }catch(...){return false;}
}
bool complete(const SessionIdentity& identity,const Timing& timing,const ClosureProof& closure,
 const Receipt& receipt,const Authentication& auth,const recovery::State& before,
 const recovery::State& after,ClockPair observed,bool referenceContinuous,bool admissionClosed,bool groupAbsent)noexcept{
 return validIdentity(identity)&&validTiming(timing)&&referenceContinuous&&admissionClosed&&groupAbsent&&closure==receipt.closure&&authenticated(identity,receipt,auth)&&
  proofWithinBudget(closure,identity,closure.generation,closure.lastGrantSequence,observed,timing)&&before==after&&recovery::terminal(before)&&
  before.boot==identity.boot.data()&&before.id==identity.unit.data()&&before.invocation==identity.invocation.data();
}
}
