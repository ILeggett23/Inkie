#include "tom_process.h"
#include "underlay.h"
#include <QJsonDocument>
#include <QStringList>
#include <QUuid>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
namespace tom {
HelperProcess::~HelperProcess(){terminate();}
bool HelperProcess::start(QString action,QString conversation,QString turn){if(running())return false;if(!QStringList{"config","create","resume","cancel","ink-create","ink-poll","ink-cancel"}.contains(action))throw std::runtime_error("Tom helper action");auto uuid=[](const QString& s){return !QUuid(s).isNull()&&QUuid(s).toString(QUuid::WithoutBraces)==s;};if(action!="config"&&(!uuid(conversation)||!uuid(turn)))throw std::runtime_error("Tom helper turn identity");
 auto helper=(application_/"bin/reader-experience1-prepare").string();struct stat st{};if(lstat(helper.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0022))throw std::runtime_error("Tom helper identity");nonce_=QUuid::createUuid().toString(QUuid::WithoutBraces);action_=std::move(action);conversation_=std::move(conversation);turn_=std::move(turn);auto command=action_.toStdString(),root=root_.string(),nonce=nonce_.toStdString(),c=conversation_.toStdString(),t=turn_.toStdString();char path[]="PATH=/usr/bin:/bin",lang[]="LANG=C.UTF-8";char* env[]{path,lang,nullptr};char* argv[]{helper.data(),const_cast<char*>("--tom"),command.data(),root.data(),nonce.data(),c.data(),t.data(),nullptr};if(action_=="config")argv[5]=nullptr;
 posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions))throw std::runtime_error("Tom helper actions");auto setup=posix_spawn_file_actions_addclosefrom_np(&actions,3);auto error=setup?setup:posix_spawn(&pid_,helper.c_str(),&actions,nullptr,argv,env);posix_spawn_file_actions_destroy(&actions);if(error){pid_=-1;throw std::runtime_error("Tom helper spawn");}killed_=false;deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(40);return true;
}
void HelperProcess::terminate()noexcept{if(running()&&!killed_){kill(pid_,SIGKILL);killed_=true;}}
std::optional<QJsonObject> HelperProcess::poll(){if(!running())return {};if(std::chrono::steady_clock::now()>=deadline_)terminate();int status{};auto result=waitpid(pid_,&status,WNOHANG);if(result==0||(result<0&&errno==EINTR))return {};if(result<0){pid_=-1;throw std::runtime_error("Tom helper wait");}pid_=-1;QJsonObject failure{{"ok",false},{"error",killed_?"Tom operation stopped; durable state retained":"Tom helper failed; durable state retained"}};if(killed_||!WIFEXITED(status)||WEXITSTATUS(status))return failure;
 auto file=root_/"tom/results"/(nonce_.toStdString()+".json");struct stat st{};if(lstat(file.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1||(st.st_mode&0777)!=0600)throw std::runtime_error("Tom result identity");auto message=QJsonDocument::fromJson(underlay::readBounded(file,2*1024*1024)).object();if(message["command_id"]!=nonce_||!message["result"].isObject())throw std::runtime_error("Tom result binding");if(unlink(file.c_str()))throw std::runtime_error("Tom result cleanup");return message["result"].toObject();
}
}
