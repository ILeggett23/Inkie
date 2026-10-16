#include "reader.h"
#include "page_server.h"
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include "durable_file.h"
namespace reader {
// Runs only on the low-priority preparer. The helper remains in this session's
// PID1 cgroup, never touches input/display, and cannot outlive cgroup drain.
underlay::Prepared preparePage(const std::filesystem::path& app,const std::filesystem::path& data,int page,const std::atomic<bool>& stop){
 auto helper=(app/"bin/reader-experience1-prepare").string();struct stat st{};
 if(lstat(helper.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0022))throw std::runtime_error("page-helper-identity");
 auto file=pagePath(data,page)/"prepared.bin",temporary=file;temporary+=".tmp";
 for(const auto& p:{file,temporary}){if(lstat(p.c_str(),&st)==0||errno!=ENOENT)throw std::runtime_error("stale-transport-needs-reconciliation");}
 // Interrupted temporaries already beside the transport belong to an earlier writer and stay put;
 // only ones this helper leaves behind are reclaimed after waitpid proves it exited.
 std::vector<std::string> before;{int d=open(file.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(d>=0){try{before=storage::interruptedWrites(d);}catch(...){close(d);throw;}close(d);}else if(errno!=ENOENT)throw std::runtime_error("transport-inspection-failed");}
 const bool ok=runPagePreparation(helper,{"--page",data.string(),std::to_string(page),app.string()},stop); /* the persistent page server, or a one-shot helper */
 if(!ok){
  // Only files absent before this helper and created in its exact private page
  // path can be reconciled after waitpid proves this helper has exited. Journals
  // and owner reservations are never touched. Unexpected type/owner stays put.
  for(const auto& p:{file,temporary}){if(lstat(p.c_str(),&st)==0){if(!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1||(st.st_mode&0022))throw std::runtime_error("unsafe-transport-retained");if(unlink(p.c_str()))throw std::runtime_error("transport-cleanup-failed");}else if(errno!=ENOENT)throw std::runtime_error("transport-inspection-failed");}
  // A helper stopped while committing leaves its durable-write temporary. Remove it, so an
  // interrupted page leaves nothing behind and later strict inspections are not refused.
  {int d=open(file.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(d>=0){try{storage::reclaimInterruptedWrites(d,before);}catch(...){close(d);throw;}close(d);}else if(errno!=ENOENT)throw std::runtime_error("transport-inspection-failed");}
  throw std::runtime_error("page-helper-failed");
 }
 auto prepared=underlay::loadPrepared(file);
 if(prepared.metadata["page"].toInt(-1)!=page)throw std::runtime_error("page-helper-result-identity");
 std::filesystem::remove(file);return prepared;
}
}
