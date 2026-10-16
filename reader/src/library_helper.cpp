#include "reader.h"
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace reader {
namespace {void runHelper(const std::filesystem::path& app,const std::filesystem::path& data,const std::filesystem::path* file,const std::atomic<bool>& stop){
 auto helper=(app/"bin/reader-experience1-prepare").string(),root=data.string();struct stat st{};
 if(lstat(helper.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0022))throw std::runtime_error("library-helper-identity");
 char path[]="PATH=/usr/bin:/bin",lang[]="LANG=C.UTF-8";char* env[]={path,lang,nullptr};std::string source=file?file->string():std::string();char* library[]={helper.data(),const_cast<char*>("--library"),root.data(),nullptr};char* single[]={helper.data(),const_cast<char*>("--import"),source.data(),root.data(),nullptr};char** args=file?single:library;
 posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions))throw std::runtime_error("library-helper-actions");int setup=posix_spawn_file_actions_addclosefrom_np(&actions,3);pid_t pid{};int error=setup?setup:posix_spawn(&pid,helper.c_str(),&actions,nullptr,args,env);posix_spawn_file_actions_destroy(&actions);if(error)throw std::runtime_error("library-helper-spawn");
 auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);bool signalled=false;int status{};
 for(;;){auto result=waitpid(pid,&status,WNOHANG);if(result==pid)break;if(result<0&&errno!=EINTR)throw std::runtime_error("library-helper-wait");if(!signalled&&(stop||std::chrono::steady_clock::now()>=end)){kill(pid,SIGKILL);signalled=true;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 if(signalled||!WIFEXITED(status)||WEXITSTATUS(status))throw std::runtime_error("library-helper-failed");
}
}
void syncLibrary(const std::filesystem::path& app,const std::filesystem::path& data,const std::atomic<bool>& stop){runHelper(app,data,nullptr,stop);}
void importFile(const std::filesystem::path& app,const std::filesystem::path& data,const std::filesystem::path& file,const std::atomic<bool>& stop){runHelper(app,data,&file,stop);}
}
