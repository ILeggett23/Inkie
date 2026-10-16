#include "frontlight.h"
#include <QJsonDocument>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <csignal>
#include <cerrno>
namespace device::frontlight {
namespace {
View run(const std::filesystem::path& app,int level,const std::atomic<bool>& stop)noexcept{View result;int pipefd[2]{-1,-1};pid_t pid=-1;bool reaped=false;try{
 if(setpriority(PRIO_PROCESS,static_cast<id_t>(syscall(SYS_gettid)),19))throw 1;auto helper=(app/"bin/reader-experience1-prepare").string();struct stat st{};if(lstat(helper.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0022))throw 1;if(pipe2(pipefd,O_CLOEXEC|O_NONBLOCK))throw 1;
 posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions))throw 1;int setup=posix_spawn_file_actions_adddup2(&actions,pipefd[1],STDOUT_FILENO);if(!setup)setup=posix_spawn_file_actions_addclosefrom_np(&actions,3);std::string number=std::to_string(level);char mode[]="--frontlight",get[]="get",set[]="set",path[]="PATH=/usr/bin:/bin",lang[]="LANG=C.UTF-8";char* args[]={helper.data(),mode,level<0?get:set,level<0?nullptr:number.data(),nullptr};char* env[]={path,lang,nullptr};int error=setup?setup:posix_spawn(&pid,helper.c_str(),&actions,nullptr,args,env);posix_spawn_file_actions_destroy(&actions);close(pipefd[1]);pipefd[1]=-1;if(error)throw 1;
 QByteArray bytes;bool killed=false;int status{};for(;;){char data[256];auto n=read(pipefd[0],data,sizeof data);if(n>0){bytes.append(data,n);if(bytes.size()>1024)throw 1;}else if(n<0&&errno!=EAGAIN&&errno!=EINTR)throw 1;auto waited=waitpid(pid,&status,WNOHANG);if(waited==pid){reaped=true;break;}if(waited<0&&errno!=EINTR)throw 1;if(stop&&!killed){kill(pid,SIGKILL);killed=true;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 for(;;){char data[256];auto n=read(pipefd[0],data,sizeof data);if(n<=0)break;bytes.append(data,n);if(bytes.size()>1024)throw 1;}close(pipefd[0]);pipefd[0]=-1;if(killed)throw 1;QJsonParseError parse;auto object=QJsonDocument::fromJson(bytes,&parse).object();if(parse.error!=QJsonParseError::NoError||object["version"]!=1)throw 1;result.requested=object["requested"].toInt(-1);result.actual=object["actual"].toInt(-1);result.maximum=object["maximum"].toInt(-1);result.power=object["power"].toInt(-1);result.available=object["available"].toBool()&&result.maximum>0&&result.maximum<=65535&&result.requested>=0&&result.requested<=result.maximum&&result.actual>=0&&result.actual<=result.maximum&&(result.power==0||result.power==4);if(!WIFEXITED(status)||WEXITSTATUS(status)||!object["ok"].toBool())result.error="Reading light could not be changed";if(!result.available)result.error="Reading light is unavailable";else if(level>=0&&(result.requested!=level||result.power!=(level?0:4)))result.error="Reading light could not be changed";
 }catch(...){result.error=stop?"Reading light action cancelled":"Reading light is unavailable";}if(pid>0&&!reaped){kill(pid,SIGKILL);for(;;){auto status=waitpid(pid,nullptr,0);if(status==pid||(status<0&&errno!=EINTR))break;}}for(int fd:pipefd)if(fd>=0)close(fd);return result;}
}
Control::~Control(){beginQuiesce();if(worker_.joinable()){if(!done_.load(std::memory_order_acquire))std::_Exit(78);worker_.join();}}
bool Control::request(const std::filesystem::path& app,int level){if(stopping_||worker_.joinable()||level< -1||level>65535)return false;recovery::v2::progress::WorkToken token;if(!tracking_.accept(token))return false;done_=false;stop_=false;writing_=level>=0;view_.error.clear();try{worker_=std::thread([this,app,level,token]()mutable{result_=run(app,level,stop_);if(!tracking_.complete(token,true))result_.error="Reading light action could not be reconciled";done_.store(true,std::memory_order_release);});}catch(...){tracking_.complete(token,true);view_.error="Reading light is unavailable";return false;}return true;}
bool Control::tick(){if(!worker_.joinable()||!done_.load(std::memory_order_acquire))return false;worker_.join();view_=std::move(result_);return true;}
}
