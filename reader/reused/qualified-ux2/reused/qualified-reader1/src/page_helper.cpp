#include "reader.h"
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace reader {
// Runs only on the low-priority preparer. The helper remains in this session's
// PID1 cgroup, never touches input/display, and cannot outlive cgroup drain.
underlay::Prepared preparePage(const std::filesystem::path& app,const std::filesystem::path& data,int page,const std::atomic<bool>& stop){
 auto helper=(app/"bin/reader-experience1-prepare").string();struct stat st{};
 if(lstat(helper.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0022))throw std::runtime_error("page-helper-identity");
 auto file=pagePath(data,page)/"prepared.bin",temporary=file;temporary+=".tmp";
 for(const auto& p:{file,temporary}){if(lstat(p.c_str(),&st)==0||errno!=ENOENT)throw std::runtime_error("stale-transport-needs-reconciliation");}
 auto path=data.string(),number=std::to_string(page),application=app.string();char envPath[]="PATH=/usr/bin:/bin",lang[]="LANG=C.UTF-8";char* env[]={envPath,lang,nullptr};
 char* args[]={helper.data(),const_cast<char*>("--page"),path.data(),number.data(),application.data(),nullptr};pid_t pid{};
 posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions))throw std::runtime_error("page-helper-actions");
 // Vendor/input/owner sockets must never leak into document preparation.
 int setup=posix_spawn_file_actions_addclosefrom_np(&actions,3);
 int spawned=setup?setup:posix_spawn(&pid,helper.c_str(),&actions,nullptr,args,env);
 posix_spawn_file_actions_destroy(&actions);if(spawned)throw std::runtime_error("page-helper-spawn");
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);bool term=false,killed=false;int status{};
 for(;;){auto w=waitpid(pid,&status,WNOHANG);if(w==pid)break;if(w<0&&errno!=EINTR)throw std::runtime_error("page-helper-wait");auto now=std::chrono::steady_clock::now();
  if(!term&&(stop||now>=deadline)){kill(pid,SIGTERM);term=true;deadline=now+std::chrono::milliseconds(20);}
  if(term&&!killed&&now>=deadline){kill(pid,SIGKILL);killed=true;}
  // A D-state child never counts as drained. Outer 100ms shutdown may fail;
  // the independent qualified owner then retains the fail-closed reservation.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 if(term||!WIFEXITED(status)||WEXITSTATUS(status)){
  // Only files absent before this helper and created in its exact private page
  // path can be reconciled after waitpid proves this helper has exited. Journals
  // and owner reservations are never touched. Unexpected type/owner stays put.
  for(const auto& p:{file,temporary}){if(lstat(p.c_str(),&st)==0){if(!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1||(st.st_mode&0022))throw std::runtime_error("unsafe-transport-retained");if(unlink(p.c_str()))throw std::runtime_error("transport-cleanup-failed");}else if(errno!=ENOENT)throw std::runtime_error("transport-inspection-failed");}
  throw std::runtime_error("page-helper-failed");
 }
 auto prepared=underlay::loadPrepared(file);
 if(prepared.metadata["page"].toInt(-1)!=page)throw std::runtime_error("page-helper-result-identity");
 std::filesystem::remove(file);return prepared;
}
}
