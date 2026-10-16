#include "underlay.h"
#include "contract.h"
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
namespace underlay {
int prepareSibling()noexcept{
 try{if(std::filesystem::exists("/run/inkbound-owner3/active"))return 78;auto helper=(std::filesystem::path(recovery::executable()).parent_path()/"reader-experience1-prepare").string();struct stat st{};
  if(lstat(helper.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&0022))return 78;
  // Preparation must not inherit XOVI/preload/QTFB/session or credential env.
  char path[]="PATH=/usr/bin:/bin",lang[]="LANG=C.UTF-8";char* env[]={path,lang,nullptr};
  char* args[]={helper.data(),const_cast<char*>("--prepare"),nullptr};pid_t pid{};if(posix_spawn(&pid,helper.c_str(),nullptr,nullptr,args,env))return 78;int status;while(waitpid(pid,&status,0)<0)if(errno!=EINTR)return 78;return WIFEXITED(status)?WEXITSTATUS(status):78;
 }catch(...){return 78;}
}
}
