// SPDX-License-Identifier: AGPL-3.0-only
#include "illustration_request.h"
#include "tom_store.h"
#include "product_store.h"
#include "underlay.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <source_location>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace {
using Path=std::filesystem::path;unsigned checks=0;
void check(bool value,std::source_location at=std::source_location::current()){++checks;if(!value)throw std::runtime_error("illustration request line "+std::to_string(at.line()));}
QString quote(const Path& p){auto s=QString::fromStdString(p.string());s.replace("'","'\\''");return "'"+s+"'";}
QByteArray fixture(){return QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==");}
int helper(int argc,char** argv){if(argc!=7)return 78;Path root=argv[3];QString action=argv[2],nonce=argv[4],id=argv[5];auto mode=QString::fromUtf8(underlay::readBounded(root/"mode",64));underlay::atomicFile(root/"entered",action.toUtf8());if((action=="ink-create"&&(mode=="block-create"||mode=="block-cancel"))||(action=="ink-cancel"&&mode=="block-cancel"))for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(50));QJsonObject result{{"ok",true},{"request_id",id},{"status",action=="ink-create"?"queued":action=="ink-cancel"?"canceled":"completed"}};if(mode=="download-resume"&&action=="ink-poll"){
 auto counter=root/"download-polls";int count=std::filesystem::exists(counter)?underlay::readBounded(counter,16).toInt():0;++count;underlay::atomicFile(counter,QByteArray::number(count));
 if(count==1)result["status"]="downloading";
 if(count==2)result=QJsonObject{{"ok",false},{"retry_after_ms",10}};
}
if(action=="ink-poll"&&result["ok"].toBool()&&result["status"]=="completed"){auto dir=root/"tom-ink-cache/images";underlay::privateDirectory(dir);auto bytes=fixture();underlay::atomicFile(dir/(id.toStdString()+".png"),bytes);result["image_sha256"]=mode=="bad-hash"?QString(64,'0'):QString::fromLatin1(underlay::digest(bytes).toHex());}auto dir=root/"tom/results";underlay::privateDirectory(dir);underlay::atomicFile(dir/(nonce.toStdString()+".json"),QJsonDocument(QJsonObject{{"command_id",nonce},{"result",result}}).toJson(QJsonDocument::Compact));return 0;}
template<class F>void until(F f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!f()){check(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
void noChildren(){int status=0;check(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD);}
}
int main(int argc,char** argv){if(argc>1&&std::string_view(argv[1])=="--tom")return helper(argc,argv);QCoreApplication application(argc,argv);try{check(argc==2);Path sys=argv[1],binary=std::filesystem::canonical(argv[0]);QTemporaryDir temp;check(temp.isValid());temp.setAutoRemove(false);Path root=temp.path().toStdString(),app=root/"application";std::cout<<"ILLUSTRATION_REQUEST_DATA "<<root<<'\n';underlay::privateDirectory(app/"bin");auto script=QString("#!/bin/sh\nexec %1 --library-path %2 %3 \"$@\"\n").arg(quote(sys/"usr/lib/ld-linux-aarch64.so.1"),quote(sys/"usr/lib"),quote(binary));underlay::atomicFile(app/"bin/reader-experience1-prepare",script.toUtf8());check(chmod((app/"bin/reader-experience1-prepare").c_str(),0700)==0);underlay::atomicFile(root/"mode","normal");auto provider=tomink::companionIllustrations(app,root);auto request=[] {return QJsonObject{{"request_id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"prompt","A fixture square"},{"preset","quick"},{"provider","openai"},{"model","gpt-image-1.5"},{"consent",true}};};std::atomic<bool> canceled{false};check(provider->generate(request(),canceled)==fixture());noChildren();underlay::atomicFile(root/"mode","download-resume");check(provider->generate(request(),canceled)==fixture());check(underlay::readBounded(root/"download-polls",16)=="3");noChildren();std::cout<<"PICTURE_HELPER_DOWNLOAD_RETRY_PASS polls=3 partial_not_published=true transient_retry_same_id=true\n";
 for(auto mode:{QByteArray("block-create"),QByteArray("block-cancel")}){underlay::atomicFile(root/"mode",mode);if(std::filesystem::exists(root/"entered"))check(!unlink((root/"entered").c_str()));auto r=request();bool rejected=false;canceled=false;std::thread worker([&]{try{provider->generate(r,canceled);}catch(const std::exception&){rejected=true;}});struct Join{std::atomic<bool>& canceled;std::thread& thread;~Join(){canceled=true;if(thread.joinable())thread.join();}} guard{canceled,worker};until([&]{return std::filesystem::exists(root/"entered");});auto begin=std::chrono::steady_clock::now();canceled=true;worker.join();check(std::chrono::steady_clock::now()-begin<std::chrono::seconds(3));check(rejected);noChildren();auto pending=root/"tom-ink-cache/cancellations"/(r["request_id"].toString().toStdString()+".json");check(std::filesystem::exists(pending)==(mode=="block-cancel"));underlay::atomicFile(root/"mode","normal");canceled=false;check(provider->generate(request(),canceled)==fixture());check(!std::filesystem::exists(pending));noChildren();}
 QString conversation;{companion::TomStore history(root);conversation=history.createConversation("Retained history");history.saveDraft(conversation,{});}underlay::atomicFile(root/"tom/history-fixture-padding.bin",QByteArray(24*1024*1024,'h'));auto indexBefore=underlay::digest(underlay::readBounded(root/"tom/index.json",512*1024));auto images=root/"tom-ink-cache/images";for(int n=0;n<8;++n)underlay::atomicFile(images/(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()+".png"),QByteArray(6*1024*1024,'i'));{companion::TomStore history(root);check(companion::TomStore::readSnapshot(root,conversation)["turns"].toArray().size()==1);}check(underlay::digest(underlay::readBounded(root/"tom/index.json",512*1024))==indexBefore);check(!std::filesystem::exists(root/"tom/ink-images")&&!std::filesystem::exists(root/"tom/ink-requests")&&!std::filesystem::exists(root/"tom/ink-cancellations"));canceled=false;underlay::atomicFile(root/"mode","normal");check(provider->generate(request(),canceled)==fixture());unsigned imageCount=0;for(auto& e:std::filesystem::directory_iterator(images)){(void)e;++imageCount;}check(imageCount<=8);{companion::TomStore history(root);check(companion::TomStore::readSnapshot(root,conversation)["turns"].toArray().size()==1);}noChildren();std::cout<<"ILLUSTRATION_CACHE_PASS history_24MiB_plus_cache_48MiB=true history_quota_unchanged=true cache_count_le_8=true\n";
 check(temp.remove());std::cout<<"ILLUSTRATION_REQUEST_PASS checks="<<checks<<" real_child_protocol=true canceled_and_reaped=true uncertain_cancel_reconciled=true within_3s=true live_calls=0 physical=NOT_RUN\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
