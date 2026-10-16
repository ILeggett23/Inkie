// SPDX-License-Identifier: AGPL-3.0-only
#include "page_server.h"
#include "e2_provisional_v2.h"
#include <spawn.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <mutex>
#include <thread>
#include <algorithm>
#include <sys/stat.h>
#include <optional>
namespace reader {
namespace {
using Clock=std::chrono::steady_clock;
enum class Outcome {Success,Failed,Fallback};
std::mutex mutex;pid_t pid=-1;int socketFd=-1;bool fresh=false;unsigned consecutiveFailures=0;PageServerStats stats;
// A helper that did not announce READY (an older or replaced binary) is not asked to serve again until it changes.
struct HelperIdentity {std::string path;dev_t dev{};ino_t ino{};long long ms{},mn{};bool operator==(const HelperIdentity&)const=default;};std::optional<HelperIdentity> refused;
std::optional<HelperIdentity> helperIdentity(const std::string& path){struct stat st{};if(::stat(path.c_str(),&st))return {};return HelperIdentity{path,st.st_dev,st.st_ino,st.st_mtim.tv_sec,st.st_mtim.tv_nsec};}
char envPath[]="PATH=/usr/bin:/bin",envLang[]="LANG=C.UTF-8";
// Kill (when asked) and reap the server. A D-state child never counts as drained, so this waits until it is reaped,
// exactly like the one-shot helper's own wait loop.
void reap(bool kill)noexcept{
 if(socketFd>=0){::shutdown(socketFd,SHUT_RDWR);::close(socketFd);socketFd=-1;}
 if(pid<0)return;
 auto deadline=Clock::now()+(kill?std::chrono::milliseconds(20):std::chrono::milliseconds(200));bool killed=false;int status{};
 if(kill)::kill(pid,SIGTERM);
 for(;;){auto w=waitpid(pid,&status,WNOHANG);if(w==pid||(w<0&&errno!=EINTR))break;if(!killed&&Clock::now()>=deadline){::kill(pid,SIGKILL);killed=true;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 pid=-1;fresh=false;
}
bool spawnServer(const std::string& helper)noexcept{
 int sv[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sv))return false;
 int child=fcntl(sv[1],F_DUPFD_CLOEXEC,10);::close(sv[1]);if(child<0){::close(sv[0]);return false;} /* never 3 itself, so dup2 clears close-on-exec */
 posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions)){::close(sv[0]);::close(child);return false;}
 // Fd 3 is the request/reply socket; vendor/input/owner sockets must never leak into document preparation.
 int setup=posix_spawn_file_actions_adddup2(&actions,child,3);if(!setup)setup=posix_spawn_file_actions_addclosefrom_np(&actions,4);
 std::string path=helper;char serveArg[]="--serve";char* args[]={path.data(),serveArg,nullptr};char* env[]={envPath,envLang,nullptr};pid_t spawned{};
 int rc=setup?setup:posix_spawn(&spawned,helper.c_str(),&actions,nullptr,args,env);posix_spawn_file_actions_destroy(&actions);::close(child);
 if(rc){::close(sv[0]);return false;}
 int flags=fcntl(sv[0],F_GETFL);if(flags<0||fcntl(sv[0],F_SETFL,flags|O_NONBLOCK)){::close(sv[0]);pid=spawned;reap(true);return false;}
 pid=spawned;socketFd=sv[0];fresh=true;
 // Handshake: a serving helper writes READY at once; anything else is stopped and refused for this binary.
 std::string ready;char chunk[16];const auto until=Clock::now()+std::chrono::seconds(3);
 while(ready.size()<6&&Clock::now()<until){pollfd p{socketFd,POLLIN,0};if(::poll(&p,1,5)<=0)continue;auto n=::read(socketFd,chunk,std::min(sizeof chunk,6-ready.size()));if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;if(n<=0)break;ready.append(chunk,std::size_t(n));}
 if(ready!="READY\n"){reap(true);refused=helperIdentity(helper);recovery::v2::e2::mark("reader:page-server-refused");return false;}
 ++stats.spawned;recovery::v2::e2::mark("reader:page-server-spawn",stats.spawned);return true;
}
Outcome request(const std::string& helper,const std::vector<std::string>& args,const std::atomic<bool>& stop)noexcept{
 try{
  std::string line;for(std::size_t i=0;i<args.size();++i){if(args[i].find_first_of("\t\n")!=std::string::npos)return Outcome::Fallback;if(i)line+='\t';line+=args[i];}line+='\n';
  if(consecutiveFailures>=3)return Outcome::Fallback; /* a server that keeps failing is not used again in this process */
  if(pid<0){if(refused&&refused==helperIdentity(helper))return Outcome::Fallback;if(!spawnServer(helper))return Outcome::Fallback;}
  for(std::size_t at=0;at<line.size();){auto n=::send(socketFd,line.data()+at,line.size()-at,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;
   if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){pollfd p{socketFd,POLLOUT,0};::poll(&p,1,5);continue;}
   if(n<=0){reap(true);return Outcome::Fallback;} /* not received: nothing was prepared, a one-shot helper may run */
   at+=std::size_t(n);}
  const auto begin=Clock::now(),deadline=begin+(fresh?std::chrono::seconds(45):std::chrono::seconds(15)); /* the first page opens and lays out */
  std::string reply;char chunk[64];
  for(;;){
   if(stop.load(std::memory_order_acquire)||Clock::now()>=deadline){reap(true);++consecutiveFailures;return Outcome::Failed;}
   pollfd p{socketFd,POLLIN,0};int ready=::poll(&p,1,5);if(ready<0&&errno!=EINTR){reap(true);++consecutiveFailures;return Outcome::Failed;}if(ready<=0)continue;
   auto n=::read(socketFd,chunk,sizeof chunk);if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
   if(n<=0){reap(true);++consecutiveFailures;return Outcome::Failed;} /* died during the request */
   reply.append(chunk,std::size_t(n));if(reply.size()>64){reap(true);++consecutiveFailures;return Outcome::Failed;}
   if(auto end=reply.find('\n');end!=std::string::npos){
    if(end+1!=reply.size()||reply.rfind("RESULT ",0)!=0){reap(true);++consecutiveFailures;return Outcome::Failed;}
    const bool ok=reply.substr(7,end-7)=="0";fresh=false;
    recovery::v2::e2::mark("reader:page-server-us",unsigned(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-begin).count()));
    if(ok){consecutiveFailures=0;++stats.served;}
    return ok?Outcome::Success:Outcome::Failed;}
  }
 }catch(...){reap(true);++consecutiveFailures;return Outcome::Failed;}
}
// The one-shot helper, as before (its own process, its own exit status).
bool oneShot(const std::string& helper,const std::vector<std::string>& args,const std::atomic<bool>& stop){
 std::vector<std::string> copy=args;std::string path=helper;std::vector<char*> argv{path.data()};for(auto& a:copy)argv.push_back(a.data());argv.push_back(nullptr);char* env[]={envPath,envLang,nullptr};pid_t child{};
 posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions))throw std::runtime_error("page-helper-actions");
 // Vendor/input/owner sockets must never leak into document preparation.
 int setup=posix_spawn_file_actions_addclosefrom_np(&actions,3);
 int spawned=setup?setup:posix_spawn(&child,helper.c_str(),&actions,nullptr,argv.data(),env);
 posix_spawn_file_actions_destroy(&actions);if(spawned)throw std::runtime_error("page-helper-spawn");
 // Whole-book EPUB layout can take several seconds on the tablet; the helper's own CPU limit is 30 s.
 auto deadline=Clock::now()+std::chrono::seconds(30);bool term=false,killed=false;int status{};
 for(;;){auto w=waitpid(child,&status,WNOHANG);if(w==child)break;if(w<0&&errno!=EINTR)throw std::runtime_error("page-helper-wait");auto now=Clock::now();
  if(!term&&(stop||now>=deadline)){::kill(child,SIGTERM);term=true;deadline=now+std::chrono::milliseconds(20);}
  if(term&&!killed&&now>=deadline){::kill(child,SIGKILL);killed=true;}
  // A D-state child never counts as drained. Outer 100ms shutdown may fail;
  // the independent qualified owner then retains the fail-closed reservation.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 return !term&&WIFEXITED(status)&&!WEXITSTATUS(status);
}
}
bool runPagePreparation(const std::string& helper,const std::vector<std::string>& args,const std::atomic<bool>& stop){
 std::lock_guard lock(mutex);
 switch(request(helper,args,stop)){
  case Outcome::Success:return true;
  case Outcome::Failed:++stats.failures;return false;
  case Outcome::Fallback:break;
 }
 ++stats.fallbacks;recovery::v2::e2::mark("reader:page-server-fallback",stats.fallbacks);return oneShot(helper,args,stop);
}
void closePageServer()noexcept{std::lock_guard lock(mutex);reap(false);}
PageServerStats pageServerStats()noexcept{std::lock_guard lock(mutex);return stats;}
}
