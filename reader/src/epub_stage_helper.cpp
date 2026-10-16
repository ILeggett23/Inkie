#include "epub_stage_helper.h"
#include "epub_view_store.h"
#include "library.h"
#include "underlay.h"
#include <QJsonDocument>
#include <QSet>
#include <QUuid>
#include <cerrno>
#include <chrono>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
namespace reader {namespace {
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Fd{int n;~Fd(){if(n>=0)close(n);}};
bool integer(const QJsonValue& v,int lo,int hi){return v.isDouble()&&v.toDouble()==v.toInt(lo-1)&&v.toInt()>=lo&&v.toInt()<=hi;}
void removeResult(const std::filesystem::path& path){struct stat st{};if(lstat(path.c_str(),&st)){need(errno==ENOENT,"epub-stage-result-inspect");return;}need(S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_nlink==1&&!(st.st_mode&0077),"epub-stage-unsafe-result-retained");need(unlink(path.c_str())==0,"epub-stage-result-remove");}
}
EpubStageResult stageEpubViewViaHelper(const std::filesystem::path& app,const std::filesystem::path& root,const QString& current,int width,int font,int currentPage,const std::atomic<bool>& stop,epub::Typography typography){
 auto before=epubviews::readSet(root,current);need(width>=64&&width<=4094&&font>=6&&font<=72&&currentPage>=0&&currentPage<before.receipt["pages"].toInt(),"epub-stage-request-range");need(!stop,"epub-stage-stopped");auto helper=(app/"bin/reader-experience1-prepare").string();struct stat st{};need(lstat(helper.c_str(),&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&!(st.st_mode&0022),"epub-stage-helper-identity");
 auto results=root/"view-results";underlay::privateDirectory(results);auto nonce=QUuid::createUuid().toString(QUuid::WithoutBraces);auto result=results/(nonce.toStdString()+".json");need(lstat(result.c_str(),&st)!=0&&errno==ENOENT,"epub-stage-result-exists");auto path=root.string(),wireCurrent=(current.isEmpty()?QString("baseline"):current).toStdString(),w=std::to_string(width),f=std::to_string(font),page=std::to_string(currentPage),n=nonce.toStdString(),application=app.string(),type=epub::token(typography).toStdString();char envPath[]="PATH=/usr/bin:/bin",lang[]="LANG=C.UTF-8";char* env[]={envPath,lang,nullptr};char* args[]={helper.data(),const_cast<char*>("--epub-stage-view"),path.data(),wireCurrent.data(),w.data(),f.data(),page.data(),n.data(),application.data(),type.data(),nullptr};
 posix_spawn_file_actions_t actions;need(posix_spawn_file_actions_init(&actions)==0,"epub-stage-actions");int setup=posix_spawn_file_actions_addclosefrom_np(&actions,3);pid_t pid{};int spawned=setup?setup:posix_spawn(&pid,helper.c_str(),&actions,nullptr,args,env);posix_spawn_file_actions_destroy(&actions);need(spawned==0,"epub-stage-spawn");auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(40);bool term=false,killed=false;int status{};
 for(;;){auto waited=waitpid(pid,&status,WNOHANG);if(waited==pid)break;need(waited>=0||errno==EINTR,"epub-stage-wait");auto now=std::chrono::steady_clock::now();if(!term&&(stop||now>=deadline)){kill(pid,SIGTERM);term=true;deadline=now+std::chrono::milliseconds(20);}if(term&&!killed&&now>=deadline){kill(pid,SIGKILL);killed=true;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 if(term||!WIFEXITED(status)||WEXITSTATUS(status)){removeResult(result);throw std::runtime_error("epub-stage-helper-failed");}
 Fd input{open(result.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};need(input.n>=0,"epub-stage-result-open");struct stat opened{};need(fstat(input.n,&opened)==0&&S_ISREG(opened.st_mode)&&opened.st_uid==getuid()&&opened.st_nlink==1&&!(opened.st_mode&0077)&&opened.st_size>0&&opened.st_size<=131072,"epub-stage-result-unsafe");QByteArray raw(opened.st_size,Qt::Uninitialized);qsizetype at=0;while(at<raw.size()){auto count=read(input.n,raw.data()+at,std::size_t(raw.size()-at));if(count<0&&errno==EINTR)continue;need(count>0,"epub-stage-result-read");at+=count;}char extra;need(read(input.n,&extra,1)==0,"epub-stage-result-size");QJsonParseError error;auto parsed=QJsonDocument::fromJson(raw,&error);need(error.error==QJsonParseError::NoError&&parsed.isObject(),"epub-stage-result-json");auto o=parsed.object();QSet<QString> fields{"version","nonce","current","width","font","from_page","source_sha","view_id","page","readingAnchor"};need(o.size()==fields.size(),"epub-stage-result-fields");for(auto i=o.begin();i!=o.end();++i)need(fields.contains(i.key()),"epub-stage-result-fields");need(o["version"]==1&&o["nonce"]==nonce&&o["current"]==current&&o["width"]==width&&o["font"]==font&&o["from_page"]==currentPage&&o["source_sha"]==before.receipt["source_sha"]&&o["view_id"].isString()&&o["readingAnchor"].isObject(),"epub-stage-result-request-binding");
 auto id=o["view_id"].toString();auto staged=epubviews::readSet(root,id);auto layout=staged.receipt["layout"].toObject();need(layout["userCss"].toString()==epub::css(typography)&&layout["width"]==width&&layout["height"]==800&&layout["fontSize"]==font&&integer(o["page"],0,staged.receipt["pages"].toInt()-1)&&staged.receipt["source_sha"]==before.receipt["source_sha"],"epub-stage-result-layout-binding");auto anchor=o["readingAnchor"].toObject();need(anchor["version"]==1&&anchor["document"]==before.receipt["document"]&&anchor["sourceSha256"]==before.receipt["source_sha"]&&anchor["engine"]=="1.28.3"&&library::validHash(anchor["chapterSourceSha256"].toString())&&integer(anchor["chapter"],0,511)&&anchor["spineItem"].isString()&&!anchor["spineItem"].toString().isEmpty()&&anchor["spineItem"].toString().size()<=2048,"epub-stage-reading-anchor");if(anchor["kind"]=="spine-start")need(currentPage==0&&anchor["chapter"]==0,"epub-stage-spine-start-binding");else need(anchor["kind"]=="reading-position"&&anchor["origin"].isObject()&&anchor["origin"].toObject()["page"]==currentPage&&!anchor["matchQuote"].toString().isEmpty(),"epub-stage-reading-position-binding");
 need(lstat(result.c_str(),&st)==0&&st.st_dev==opened.st_dev&&st.st_ino==opened.st_ino,"epub-stage-result-replaced");removeResult(result);return {id,o["page"].toInt(),anchor};
}
}
