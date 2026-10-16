// SPDX-License-Identifier: AGPL-3.0-only
// Product store reopen cache: a reopen of an unchanged features file reuses the validated state; a file replaced
// by another writer (new inode), edited in place (new ctime) or opened with a different binding is read and
// validated again, so corruption and binding changes are still refused.
#include "product_store.h"
#include "underlay.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
static unsigned checks=0;
static void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
template<class F>static bool throws(F f,const char* what){try{f();}catch(const std::exception& e){return std::string(e.what()).find(what)!=std::string::npos;}return false;}
static QByteArray encoded(const QJsonObject& s){auto b=QJsonDocument(s).toJson(QJsonDocument::Compact);return QJsonDocument(QJsonObject{{"payload",s},{"sha256",product::sha256(b)}}).toJson(QJsonDocument::Compact);}
int main(int argc,char** argv){QCoreApplication app(argc,argv);
 try{
  QTemporaryDir temp;check(temp.isValid(),"temp");const std::filesystem::path dir=std::filesystem::path(temp.path().toStdString())/"features";
  const QJsonObject binding{{"id","doc-1"},{"revision","abc"},{"format","pdf"},{"pages",12},{"layoutId","fixed-v1"}};
  QJsonArray strokes;for(int i=0;i<6000;++i)strokes.append(QJsonObject{{"id",QString("s%1").arg(i)},{"points",QJsonArray{i,i+1,i+2,i+3,i+4,i+5,i+6,i+7}}});
  {product::Store s(dir,binding);s.transaction([&](QJsonObject& st){auto ann=binding;ann["layers"]=QJsonArray{QJsonObject{{"id","l1"}}};ann["activeLayer"]="l1";ann["strokes"]=strokes;for(auto k:{"highlights","notes","undo","redo"})ann[k]=QJsonArray{};st["annotation"]=ann;});check(s.snapshot()["revision"].toInt()==1,"first commit");}
  auto timed=[&](const QJsonObject& b){auto t0=std::chrono::steady_clock::now();product::Store s(dir,b);auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t0).count();return std::make_pair(s.snapshot(),us);};
  auto [a,cold]=timed(binding);check(a["revision"].toInt()==1&&a["annotation"].toObject()["strokes"].toArray().size()==6000,"reopen state");
  auto [b,warm]=timed(binding);check(b==a,"cached reopen identical");
  /* another writer replaces the file (atomic rename: new inode) */
  auto next=a;next["revision"]=5;underlay::atomicFile(dir/"features.json",encoded(next));
  auto [c,cold2]=timed(binding);check(c["revision"].toInt()==5,"replaced file is read again");
  auto [d,warm2]=timed(binding);check(d==c,"and then cached");cold=cold2;warm=warm2;
  /* in-place corruption keeps the inode but moves ctime: must be read and refused */
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  {int fd=::open((dir/"features.json").c_str(),O_WRONLY|O_CLOEXEC);check(fd>=0,"open in place");check(::pwrite(fd,"X",1,20)==1,"corrupt byte");::close(fd);}
  check(throws([&]{product::Store s(dir,binding);},"product"),"in-place corruption refused despite the cache");
  underlay::atomicFile(dir/"features.json",encoded(next));
  {product::Store s(dir,binding);check(s.snapshot()["revision"].toInt()==5,"repaired file reads");}
  check(throws([&]{auto other=binding;other["id"]="doc-2";product::Store s(dir,other);},"binding"),"a different binding is refused despite the cache");
  /* commits by this process keep the cache current */
  {product::Store s(dir,binding);s.transaction([&](QJsonObject& st){st["settings"]=QJsonObject{{"k",1}};});}
  {product::Store s(dir,binding);check(s.snapshot()["revision"].toInt()==6&&s.snapshot()["settings"].toObject()["k"].toInt()==1,"own commit visible on reopen");}
  /* the exclusive lock is still taken on a cached open */
  {product::Store s(dir,binding);check(throws([&]{product::Store t(dir,binding);},"product-store-already-open"),"cached open still holds the lock");}
  QJsonObject marked;{product::Store s(dir,binding);auto old=s.notesNormalization().revision;s.markNotesNormalized(old);check(s.notesNormalization().verified,"normalized hint stored for exact revision");s.transaction([](QJsonObject& state){state["settings"]=QJsonObject{{"k",2}};});s.markNotesNormalized(old);check(!s.notesNormalization().verified,"raw transaction and stale normalization stamp invalidate hint");s.markNotesNormalized(s.notesNormalization().revision);marked=s.snapshot();}
  {product::Store s(dir,binding);check(s.notesNormalization().verified,"unchanged file reuses normalized hint");}underlay::atomicFile(dir/"features.json",encoded(marked));{product::Store s(dir,binding);check(!s.notesNormalization().verified,"same revision with new inode does not inherit normalized hint");}
  for(unsigned i=0;i<product::Store::MaxCacheEntries+4;++i){auto path=std::filesystem::path(temp.path().toStdString())/("cache-"+std::to_string(i));product::Store s(path,binding);s.transaction([](QJsonObject& state){state["settings"]=QJsonObject{{"padding",QString(3*1024*1024,'x')}};});auto stats=product::Store::cacheStats();check(stats.entries<=product::Store::MaxCacheEntries&&stats.encodedBytes<=product::Store::MaxCacheBytes,"LRU cache obeys entry and encoded byte bounds");}
  {auto path=std::filesystem::path(temp.path().toStdString())/"cache-0";auto before=underlay::readBounded(path/"features.json",product::Store::MaxBytes);product::Store s(path,binding);check(s.snapshot()["settings"].toObject()["padding"].toString().size()==3*1024*1024&&underlay::readBounded(path/"features.json",product::Store::MaxBytes)==before,"evicted durable state reopens unchanged");}
  auto large=std::filesystem::path(temp.path().toStdString())/"larger-than-cache";{product::Store s(large,binding);s.transaction([](QJsonObject& state){state["settings"]=QJsonObject{{"padding",QString(product::Store::MaxCacheBytes+1024,'y')}};});}auto stats=product::Store::cacheStats();{product::Store s(large,binding);check(s.snapshot()["settings"].toObject()["padding"].toString().size()==product::Store::MaxCacheBytes+1024,"large uncached store remains valid");}auto after=product::Store::cacheStats();check(after.entries<=product::Store::MaxCacheEntries&&after.encodedBytes<=product::Store::MaxCacheBytes&&after.hits==stats.hits&&after.misses==stats.misses+1,"large files bypass cache without relaxing store cap");
  std::printf("PRODUCT_STORE_CACHE_PASS checks=%u cold_open_us=%lld cached_open_us=%lld replaced_reread=true in_place_corruption_refused=true binding_refused=true lock_kept=true\n",checks,(long long)cold,(long long)warm);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"PRODUCT_STORE_CACHE_FAILED %s checks=%u\n",e.what(),checks);return 1;}
}
