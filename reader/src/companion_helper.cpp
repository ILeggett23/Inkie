#include "reader.h"
#include <QJsonDocument>
#include <QUuid>
#include <QStringList>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace reader {
QJsonObject companionCommand(const std::filesystem::path& app,const std::filesystem::path& data,QString action,QString expectedPairId,QString expectedAccount,const std::atomic<bool>& stop){
 if(setpriority(PRIO_PROCESS,static_cast<id_t>(syscall(SYS_gettid)),19))throw std::runtime_error("companion-priority");
 if(!QStringList{"status","pair","poll","approve","reject","sync","retry","unpair"}.contains(action))throw std::runtime_error("companion-command");
 auto helper=(app/"bin/reader-experience1-prepare").string(),root=data.string(),nonce=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),command=action.toStdString(),pair=expectedPairId.toStdString(),account=expectedAccount.toStdString();struct stat st{};
 if(lstat(helper.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0022))throw std::runtime_error("companion-helper-identity");
 char path[]="PATH=/usr/bin:/bin",lang[]="LANG=C.UTF-8";char* env[]={path,lang,nullptr};char* args[]={helper.data(),const_cast<char*>("--companion"),command.data(),root.data(),nonce.data(),pair.data(),account.data(),nullptr};
 if(action!="approve"&&action!="reject")args[5]=nullptr;
 posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions))throw std::runtime_error("companion-helper-actions");int setup=posix_spawn_file_actions_addclosefrom_np(&actions,3);pid_t pid{};int error=setup?setup:posix_spawn(&pid,helper.c_str(),&actions,nullptr,args,env);posix_spawn_file_actions_destroy(&actions);if(error)throw std::runtime_error("companion-helper-spawn");
 auto end=std::chrono::steady_clock::now()+std::chrono::seconds(40);bool signalled=false;int status{};
 for(;;){auto result=waitpid(pid,&status,WNOHANG);if(result==pid)break;if(result<0&&errno!=EINTR)throw std::runtime_error("companion-helper-wait");if(!signalled&&(stop||std::chrono::steady_clock::now()>=end)){kill(pid,SIGKILL);signalled=true;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 if(signalled||!WIFEXITED(status)||WEXITSTATUS(status))throw std::runtime_error("companion-helper-failed");
 auto file=data/"companion/results"/(nonce+".json");if(lstat(file.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1||(st.st_mode&0777)!=0600)throw std::runtime_error("companion-result-identity");auto result=QJsonDocument::fromJson(underlay::readBounded(file,16384)).object();if(result["command_id"].toString()!=QString::fromStdString(nonce)||!result["status"].isObject())throw std::runtime_error("companion-result-binding");if(unlink(file.c_str()))throw std::runtime_error("companion-result-cleanup");return result["status"].toObject();
}
}
