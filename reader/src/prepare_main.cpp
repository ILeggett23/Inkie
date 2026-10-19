#include "source_file.h"
#include "epub_typography.h"
#include "underlay.h"
#include "frontlight_native.h"
#include "reader.h"
#include "semantic.h"
#include "library.h"
#include "import_inspector.h"
#include "receiver.h"
#include <QCoreApplication>
#include "tom_helper_main.h"
#include "release_policy.h"
#include "prepare_document_view.h"
#include "prepare_epub_view.h"
#include "epub_annotation_cache.h"
#include "epub_annotation_reflow.h"
#include "product_services.h"
#include "page_thumbnails_render.h"
#include <QUuid>
#include <QSslSocket>
#include "document_validation.h"
#include "core/document/MuPdfDocumentEngine.h"
#include <QImage>
#include <QBuffer>
#include <QFileInfo>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <cstdio>
#include <string_view>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
static int pageRequest(int argc,char** argv,bool initial){
 underlay::setTransientPreparedTransport(!initial); /* page transport only; the initial prepare keeps durable writes */
 bool viewPage=(argc==6||argc==7)&&std::string_view(argv[1])=="--view-page"; /* 7: the orientation a turned page is shown in (docs/LANDSCAPE.md) */bool epubViewPage=(argc==6||argc==7)&&std::string_view(argv[1])=="--epub-view-page";unsigned orientation=0;if((viewPage||epubViewPage)&&argc==7){const std::string_view o=argv[6];if(o=="90")orientation=90;else if(o=="270")orientation=270;else return 78;}bool page=(argc==5&&std::string_view(argv[1])=="--page")||viewPage||epubViewPage;if(!initial&&!page)return 78;
 try{char path[4096];auto n=readlink("/proc/self/exe",path,sizeof(path)-1);if(n<=0||n>=4095)return 78;path[n]=0;
  auto root=std::filesystem::path(path).parent_path().parent_path();underlay::Prepared p;std::filesystem::path pageStorage;
  if(initial)p=reader::initialPrepare(root,underlay::DataPath);
  else{std::string number=argv[3];std::size_t used{};int index=std::stoi(number,&used);if(used!=number.size()||!std::filesystem::path(argv[2]).is_absolute()||!std::filesystem::path(argv[4]).is_absolute())return 78;if(epubViewPage){auto receipt=reader::epubviews::readSet(argv[2],QString::fromLocal8Bit(argv[5]));pageStorage=reader::epubviews::turnedStoragePath(argv[2],receipt,index,orientation);p=reader::prepareEpubView(argv[4],argv[2],index,receipt,orientation);}else if(viewPage){auto receipt=reader::viewstore::readSet(argv[2],QString::fromLocal8Bit(argv[5]));pageStorage=reader::viewstore::turnedStoragePath(argv[2],receipt,index,orientation);p=reader::preparePdfView(argv[4],argv[2],index,receipt,orientation);}else{pageStorage=reader::pagePath(argv[2],index);p=underlay::prepare(argv[4],pageStorage,index,true,argv[2]);}}
  if(page&&p.metadata["schema"]!=4){try{auto descriptor=library::descriptor(argv[2]);auto source=descriptor.isEmpty()?std::filesystem::path(argv[4])/"fixtures/inkbound-device-test.pdf":library::sourcePath(argv[2],descriptor);auto geometry=semantic::extract(source,p.metadata["page"].toInt(),p.binding,p.metadata);p.text=geometry;semantic::saveGeometry(pageStorage/"semantic-text.json",geometry);}catch(const std::exception& e){std::fprintf(stderr,"TEXT_PREPARATION_UNAVAILABLE reason=%s\n",e.what());}}
  if(page&&p.metadata["documentFormat"]=="epub"){auto view=reader::epubviews::readSet(argv[2],epubViewPage?QString::fromLocal8Bit(argv[5]):QString{});auto geometry=reader::epubannotations::projectCache(argv[2],view,p.text,p.binding);if(geometry)semantic::saveGeometry(pageStorage/"semantic-text.json",geometry);}
  std::printf("READER_PREPARED page=%d pages=%d replayed_operations=%llu vendor_calls=0 input_opens=0\n",p.metadata["page"].toInt(),p.metadata["pages"].toInt(),(unsigned long long)p.lastId);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"PREPARATION_FAILED reason=%s\n",e.what());return 78;}
}
// D2 page turns, as the stock reader's long-lived renderer: --serve keeps one helper per open document. Requests arrive
// on fd 3 (a socket from the reader) as one tab-separated argument line per page (--page / --view-page /
// --epub-view-page and their usual arguments); each runs exactly the one-shot path above, whose caches keep the
// verified source and laid-out document, and is answered "RESULT <exit code>". EOF ends the server; it also retires
// before its CPU limit so the reader starts a fresh one.
static int serve(char* self){
 std::string buffer;char chunk[4096];
 {const char ready[]="READY\n";for(std::size_t at=0;at<sizeof ready-1;){auto n=write(3,ready+at,sizeof ready-1-at);if(n<0&&errno==EINTR)continue;if(n<=0)return 78;at+=std::size_t(n);}} /* the reader falls back to one-shot helpers without it */
 for(;;){
  std::size_t end;while((end=buffer.find('\n'))==std::string::npos){auto n=read(3,chunk,sizeof chunk);if(n<0&&errno==EINTR)continue;if(n<=0)return 0;buffer.append(chunk,std::size_t(n));if(buffer.size()>65536)return 78;}
  std::string line=buffer.substr(0,end);buffer.erase(0,end+1);
  std::vector<std::string> parts;for(std::size_t at=0;;){auto tab=line.find('\t',at);parts.push_back(line.substr(at,tab==std::string::npos?std::string::npos:tab-at));if(tab==std::string::npos)break;at=tab+1;}
  const bool known=(parts.size()==4&&parts[0]=="--page")||(parts.size()==5&&(parts[0]=="--view-page"||parts[0]=="--epub-view-page"))||(parts.size()==6&&(parts[0]=="--view-page"||parts[0]=="--epub-view-page")); /* 6: with the orientation */
  int rc=78;if(known){std::vector<char*> args{self};for(auto& part:parts)args.push_back(part.data());args.push_back(nullptr);rc=pageRequest(int(args.size()-1),args.data(),false);}
  std::fflush(stdout);std::fflush(stderr);
  const std::string reply="RESULT "+std::to_string(rc)+"\n";for(std::size_t at=0;at<reply.size();){auto n=write(3,reply.data()+at,reply.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0)return 78;at+=std::size_t(n);}
  struct rusage use{};if(!getrusage(RUSAGE_SELF,&use)&&use.ru_utime.tv_sec+use.ru_stime.tv_sec>=540)return 0;
 }
}
int main(int argc,char**argv){
 umask(0077);struct rlimit coreLimit{0,0};if(setrlimit(RLIMIT_CORE,&coreLimit))return 78;
#ifndef __SANITIZE_ADDRESS__
 struct rlimit memoryLimit{1024ULL*1024*1024,1024ULL*1024*1024};if(setrlimit(RLIMIT_AS,&memoryLimit))return 78;
#endif
 const bool serving=argc==2&&std::string_view(argv[1])=="--serve";
 struct rlimit cpuLimit{serving?600u:30u,serving?600u:30u};if(setrlimit(RLIMIT_CPU,&cpuLimit))return 78;
 if(serving)return serve(argv[0]);
 if((argc==3||argc==4)&&std::string_view(argv[1])=="--frontlight"){device::frontlight::Native light;bool ok=false;if(argc==3&&std::string_view(argv[2])=="get")ok=light.read().available;else if(argc==4&&std::string_view(argv[2])=="set"){int level{};auto end=argv[3]+std::strlen(argv[3]);auto parsed=std::from_chars(argv[3],end,level);if(parsed.ec==std::errc{}&&parsed.ptr==end)ok=light.set(level);}auto state=light.read();auto output=QJsonDocument(QJsonObject{{"version",1},{"ok",ok},{"available",state.available},{"requested",state.requested},{"actual",state.actual},{"maximum",state.maximum},{"power",state.power}}).toJson(QJsonDocument::Compact);std::fwrite(output.data(),1,output.size(),stdout);return ok?0:78;}

 if(argc==4&&std::string_view(argv[1])=="--cover"){QCoreApplication application(argc,argv);try{const auto root=std::filesystem::path(argv[2]);const auto expected=QString::fromLocal8Bit(argv[3]);if(!root.is_absolute()||!library::validHash(expected))return 78;const auto descriptor=library::descriptor(root);if(descriptor.isEmpty()||descriptor["sha256"]!=expected||descriptor["id"]!=QString::fromStdString(root.filename().string()))return 78;const auto source=library::sourcePath(root,descriptor);if(reader::source::hashFile(source,library::MaxSourceBytes).digest.toHex()!=expected.toLatin1())return 78;const auto cover=library::inspectDocument(source,descriptor["format"].toString()).thumbnail;auto output=QJsonDocument(QJsonObject{{"version",1},{"source",expected},{"png",QString::fromLatin1(cover.toBase64())}}).toJson(QJsonDocument::Compact);return std::fwrite(output.data(),1,output.size(),stdout)==std::size_t(output.size())?0:78;}catch(...){return 78;}}
 if(argc>1&&(std::string_view(argv[1])=="--tom"||std::string_view(argv[1])=="--companion")){
  try{auto app=std::filesystem::canonical("/proc/self/exe").parent_path().parent_path();if(!release::policy(app).networking){std::fputs("CONNECTED_UNAVAILABLE offline_release\n",stderr);return 78;}}
  catch(const std::exception&){std::fputs("CONNECTED_UNAVAILABLE invalid_release_policy\n",stderr);return 78;}
 }
 if(argc>=5&&std::string_view(argv[1])=="--tom"){QCoreApplication application(argc,argv);return tom::helperMain(argc,argv);}
 if(argc>=5&&std::string_view(argv[1])=="--companion"){
  QCoreApplication application(argc,argv);auto root=std::filesystem::path(argv[3]);auto nonce=QString::fromLocal8Bit(argv[4]);if(!root.is_absolute()||QUuid(nonce).isNull()||QUuid(nonce).toString(QUuid::WithoutBraces)!=nonce)return 78;
  auto action=std::string_view(argv[2]);QJsonObject report;bool ok=true;QByteArray before;
  try{before=underlay::digest(underlay::readBounded(root/"catalogue.json",2*1024*1024));}catch(...){}
  try{companion::VerifiedHttpTransport http;companion::Receiver receiver(root,http,library::inspectDocument);
   try{if(action!="status"&&action!="retry"&&action!="unpair"&&!QSslSocket::supportsSsl())throw std::runtime_error("tls-unavailable");if(action=="status"){}else if(action=="unpair")receiver.unpair();else if(action=="pair")receiver.startPairing("Inkbound Reader");else if(action=="poll")receiver.pollPairing();else if(action=="sync")receiver.syncBurst();else if(action=="retry")receiver.retryTransfer();else if(action=="approve"||action=="reject"){auto shown=receiver.status();if(argc!=7||shown["pairing_id"].toString()!=QString::fromLocal8Bit(argv[5])||shown["account"].toString()!=QString::fromLocal8Bit(argv[6]))throw std::runtime_error("pairing-display-changed");receiver.approvePairing(action=="approve");}else throw std::runtime_error("companion-command");}
   catch(...){ok=false;}report=receiver.status();
  }catch(...){ok=false;report={{"state","storage-error-reopen-required"}};}
  if(action!="status"&&action!="retry"&&action!="unpair"&&!QSslSocket::supportsSsl()){report["state"]="tls-unavailable";ok=false;}
  report["operation_failed"]=!ok;try{report["library_changed"]=underlay::digest(underlay::readBounded(root/"catalogue.json",2*1024*1024))!=before;}catch(...){report["library_changed"]=false;}
  try{auto results=root/"companion/results";underlay::privateDirectory(results);underlay::atomicFile(results/(nonce.toStdString()+".json"),QJsonDocument(QJsonObject{{"command_id",nonce},{"status",report}}).toJson(QJsonDocument::Compact));std::puts("COMPANION_COMMAND_FINISHED");return 0;}catch(...){std::fputs("COMPANION_COMMAND_FAILED\n",stderr);return 78;}
 }
 bool initial=argc==2&&std::string_view(argv[1])=="--prepare"&&getuid()==0;
 if((argc==4&&std::string_view(argv[1])=="--import")||(argc==3&&std::string_view(argv[1])=="--library")){
  try{auto data=std::filesystem::path(argv[argc-1]);library::Store store(data);
   if(argc==4)store.importDocument(argv[2],QFileInfo(QString::fromLocal8Bit(argv[2])).completeBaseName(),library::inspectDocument);
   else{const auto inbox=data/"inbox",archive=inbox/"imported";underlay::privateDirectory(inbox);underlay::privateDirectory(archive);QJsonObject failures;try{failures=QJsonDocument::fromJson(underlay::readBounded(data/"inbox-failures.json",65536)).object();}catch(...){}QJsonObject nextFailures;unsigned count=0;QJsonArray rejected;
    for(auto& e:std::filesystem::directory_iterator(inbox)){if(e.path()==archive)continue;if(++count>32)throw std::runtime_error("inbox-capacity");const auto name=QString::fromStdString(e.path().filename().string());QString stamp;
     try{if(e.is_symlink()||!e.is_regular_file())throw std::runtime_error("inbox-file-type");struct stat before{};if(lstat(e.path().c_str(),&before))throw std::runtime_error("inbox-stat");stamp=QString("%1:%2:%3:%4:%5:%6:%7").arg(qulonglong(before.st_dev)).arg(qulonglong(before.st_ino)).arg(qlonglong(before.st_size)).arg(qlonglong(before.st_mtim.tv_sec)).arg(qlonglong(before.st_mtim.tv_nsec)).arg(qlonglong(before.st_ctim.tv_sec)).arg(qlonglong(before.st_ctim.tv_nsec));if(failures[name]==stamp){nextFailures[name]=stamp;rejected.append(name);continue;}
      auto imported=store.importDocument(e.path(),QString::fromStdString(e.path().stem().string()),library::inspectDocument);struct stat after{};if(lstat(e.path().c_str(),&after)||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino||before.st_size!=after.st_size||before.st_mtim.tv_sec!=after.st_mtim.tv_sec||before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec||before.st_ctim.tv_sec!=after.st_ctim.tv_sec||before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec)continue;
      const auto archiveId=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();const auto destination=archive/(archiveId+e.path().extension().string());underlay::atomicFile(archive/(archiveId+".json"),QJsonDocument(QJsonObject{{"filename",name},{"document",imported.id},{"sha256",imported.sha256},{"format",imported.format}}).toJson(QJsonDocument::Compact));std::filesystem::rename(e.path(),destination);for(const auto& dir:{inbox,archive}){int fd=open(dir.c_str(),O_DIRECTORY|O_RDONLY|O_CLOEXEC);if(fd<0||fsync(fd)){if(fd>=0)close(fd);throw std::runtime_error("inbox-archive-sync");}close(fd);}
     }catch(...){rejected.append(name);if(!stamp.isEmpty())nextFailures[name]=stamp;}}
    auto writeChanged=[&](const auto& file,const QByteArray& bytes){try{if(underlay::readBounded(file,65536)==bytes)return;}catch(...){}underlay::atomicFile(file,bytes);};writeChanged(data/"inbox-failures.json",QJsonDocument(nextFailures).toJson(QJsonDocument::Compact));writeChanged(data/"import-errors.json",QJsonDocument(rejected).toJson(QJsonDocument::Compact));}
   std::puts("LIBRARY_DURABLE_READY");return 0;
  }catch(const std::exception&){std::fputs("LIBRARY_IMPORT_FAILED\n",stderr);return 78;}
 }
 if((argc==9||argc==10||argc==11)&&std::string_view(argv[1])=="--epub-stage-view"){ /* argv[9]: optional typography token (epub_typography.h); argv[10]: the layout height, 800 unless a landscape layout (docs/LANDSCAPE.md) */
  try{auto root=std::filesystem::path(argv[2]),app=std::filesystem::path(argv[8]);auto old=QString::fromLocal8Bit(argv[3]),nonce=QString::fromLocal8Bit(argv[7]);if(old=="baseline")old.clear();if(!root.is_absolute()||!app.is_absolute()||QUuid(nonce).isNull()||QUuid(nonce).toString(QUuid::WithoutBraces)!=nonce)return 78;auto number=[](const char* value){std::string text(value);std::size_t used{};int n=std::stoi(text,&used);if(used!=text.size()||std::to_string(n)!=text)throw std::runtime_error("epub-stage-number");return n;};int width=number(argv[4]),font=number(argv[5]),from=number(argv[6]);std::optional<epub::Typography> typography=epub::Typography{};if(argc>=10)typography=epub::fromToken(QString::fromLatin1(argv[9]));if(!typography)throw std::runtime_error("epub-stage-typography");const int height=argc==11?number(argv[10]):800;if(height<64||height>4094)throw std::runtime_error("epub-stage-height");
  auto staged=reader::stageEpubView(app,root,old,{width,height,font,epub::css(*typography)},from);auto descriptor=library::descriptor(root);if(std::filesystem::exists(root/"features/features.json")){product::Services features(root/"features");auto annotation=features.store().snapshot()["annotation"].toObject();auto target=reader::epubviews::readSet(root,staged.viewId);auto cache=reader::epubannotations::derive(root,annotation,target);reader::epubannotations::saveCache(root,target,cache);}auto result=root/"view-results";underlay::privateDirectory(result);auto path=result/(nonce.toStdString()+".json");struct stat st{};if(lstat(path.c_str(),&st)==0||errno!=ENOENT)throw std::runtime_error("epub-stage-result-exists");QJsonObject response{{"version",1},{"nonce",nonce},{"current",old},{"width",width},{"font",font},{"from_page",from},{"source_sha",descriptor["sha256"]},{"view_id",staged.viewId},{"page",staged.targetPage},{"readingAnchor",staged.readingAnchor}};underlay::atomicFile(path,QJsonDocument(response).toJson(QJsonDocument::Compact));return 0;}catch(const std::exception& e){std::fprintf(stderr,"EPUB_STAGE_FAILED reason=%s\n",e.what());return 78;}
 }
 if(argc==6&&std::string_view(argv[1])=="--thumbnails"){ /* Page overview: small gray pages on stdout only (page_thumbnails.h) */
  try{if(setpriority(PRIO_PROCESS,0,10))return 78;auto root=std::filesystem::path(argv[2]);auto view=QString::fromLocal8Bit(argv[3]);if(view=="baseline")view.clear();if(!root.is_absolute())return 78;
   auto number=[](const char* value){std::string text(value);std::size_t used{};int n=std::stoi(text,&used);if(used!=text.size()||std::to_string(n)!=text)throw std::runtime_error("thumbnail-number");return n;};
   auto wire=thumbs::encode(thumbs::renderBatch(root,view,number(argv[4]),number(argv[5])));qsizetype at=0;
   while(at<wire.size()){auto n=write(1,wire.constData()+at,std::size_t(wire.size()-at));if(n<0&&errno==EINTR)continue;if(n<=0)return 78;at+=n;}return 0;}
  catch(const std::exception& e){std::fprintf(stderr,"THUMBNAILS_FAILED reason=%s\n",e.what());return 78;}
 }
 return pageRequest(argc,argv,initial);
}
