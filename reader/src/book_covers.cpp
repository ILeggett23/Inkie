#include "book_covers.h"
#include "library.h"
#include <algorithm>
#include <QBuffer>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
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
namespace covers {namespace {
std::shared_ptr<const Image> run(const Key& key,const std::atomic<bool>& stop,bool& known)noexcept{int fds[2]{-1,-1};pid_t pid=-1;bool reaped=false;std::shared_ptr<const Image> result;known=true;try{
 if(setpriority(PRIO_PROCESS,static_cast<id_t>(syscall(SYS_gettid)),19))throw 1;const auto executable=key.app/"bin/reader-experience1-prepare";struct stat st{};if(lstat(executable.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0022)||!key.data.is_absolute()||!library::validId(key.document)||!library::validHash(key.source))throw 1;if(pipe2(fds,O_CLOEXEC))throw 1;const int flags=fcntl(fds[0],F_GETFL);if(flags<0||fcntl(fds[0],F_SETFL,flags|O_NONBLOCK))throw 1;
 posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions))throw 1;int setup=posix_spawn_file_actions_adddup2(&actions,fds[1],STDOUT_FILENO);if(!setup)setup=posix_spawn_file_actions_addclosefrom_np(&actions,3);auto helper=executable.string(),data=key.data.string(),source=key.source.toStdString();char mode[]="--cover",path[]="PATH=/usr/bin:/bin",lang[]="LANG=C.UTF-8";char* args[]={helper.data(),mode,data.data(),source.data(),nullptr};char* env[]={path,lang,nullptr};const int error=setup?setup:posix_spawn(&pid,helper.c_str(),&actions,nullptr,args,env);posix_spawn_file_actions_destroy(&actions);close(fds[1]);fds[1]=-1;if(error)throw 1;
 QByteArray bytes;bool killed=false;int status{};const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);for(;;){char buffer[8192];const auto n=read(fds[0],buffer,sizeof buffer);if(n>0){if(bytes.size()+n>2*1024*1024)throw 1;bytes.append(buffer,n);}else if(n<0&&errno!=EAGAIN&&errno!=EINTR)throw 1;const auto waited=waitpid(pid,&status,WNOHANG);if(waited==pid){reaped=true;break;}if(waited<0&&errno!=EINTR)throw 1;if(!killed&&(stop||std::chrono::steady_clock::now()>=deadline)){kill(pid,SIGKILL);killed=true;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 for(;;){char buffer[8192];const auto n=read(fds[0],buffer,sizeof buffer);if(n<=0)break;if(bytes.size()+n>2*1024*1024)throw 1;bytes.append(buffer,n);}if(killed||!WIFEXITED(status)||WEXITSTATUS(status)||stop)throw 1;QJsonParseError parse;auto doc=QJsonDocument::fromJson(bytes,&parse);auto object=doc.object();if(parse.error!=QJsonParseError::NoError||object["version"]!=1||object["source"]!=key.source||!object["png"].isString())throw 1;auto png=QByteArray::fromBase64(object["png"].toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);if(png.isEmpty()||png.size()>1024*1024)throw 1;QBuffer input(&png);if(!input.open(QIODevice::ReadOnly))throw 1;QImageReader decoder(&input,"PNG");auto size=decoder.size();if(size.width()<1||size.height()<1||size.width()>Width||size.height()>Height)throw 1;auto image=decoder.read().convertToFormat(QImage::Format_RGB32);if(image.isNull()||image.size()!=size)throw 1;auto out=std::make_shared<Image>();out->width=size.width();out->height=size.height();out->pixels.resize(std::size_t(out->width)*out->height);for(int y=0;y<out->height;++y)std::copy_n(reinterpret_cast<const std::uint32_t*>(image.constScanLine(y)),out->width,out->pixels.data()+std::size_t(y)*out->width);result=out;
 }catch(...){}if(pid>0&&!reaped){kill(pid,SIGKILL);for(;;){const auto waited=waitpid(pid,nullptr,0);if(waited==pid){reaped=true;break;}if(waited<0&&errno==EINTR)continue;break;}known=reaped;}for(int fd:fds)if(fd>=0)close(fd);return result;}
}
Service::~Service(){beginQuiesce();if(worker_.joinable())worker_.join();}
std::shared_ptr<const Image> Service::find(const QString& id,const QString& source){auto found=cache_.find(identity(id,source));if(found==cache_.end())return {};found->used=++age_;return found->image;}
bool Service::request(const Key& key){if(stopping_||worker_.joinable()||!library::validId(key.document)||!library::validHash(key.source)||cache_.contains(identity(key.document,key.source))||refused_.contains(identity(key.document,key.source)))return false;recovery::v2::progress::WorkToken token;if(!tracking_.accept(token))return false;running_=key;stop_=false;done_=false;result_.reset();try{worker_=std::thread([this,key,token]()mutable{bool known=true;result_=run(key,stop_,known);if(!tracking_.complete(token,known))result_.reset();done_.store(true,std::memory_order_release);});}catch(...){tracking_.complete(token,true);return false;}return true;}
QString Service::collect(){if(!worker_.joinable()||!done_.load(std::memory_order_acquire))return {};worker_.join();QString arrived;if(result_&&!stop_){while(cache_.size()>=CacheLimit){auto oldest=cache_.begin();for(auto i=cache_.begin();i!=cache_.end();++i)if(i->used<oldest->used)oldest=i;cache_.erase(oldest);}cache_.insert(identity(running_.document,running_.source),{result_,++age_});arrived=running_.document;}else if(!stop_){if(refused_.size()>=512)refused_.clear();refused_.insert(identity(running_.document,running_.source));++failures_;}result_.reset();return arrived;}
}
