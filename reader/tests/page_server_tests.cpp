// SPDX-License-Identifier: AGPL-3.0-only
// Persistent page server (stock-style long-lived renderer): pages prepared through one "--serve" helper are
// byte-identical to one-shot helpers (binding, background, ink, text); one process serves every page; closing reaps
// it and a later page starts a fresh one; a helper that does not announce READY falls back to one-shot helpers.
#include "reader.h"
#include "page_server.h"
#include "semantic.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
using Path=std::filesystem::path;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"PAGE_SERVER_CHECK_FAILED %s:%d: %s\n",__FILE__,__LINE__,#x);std::fflush(stderr);throw std::runtime_error(#x);}}while(0)
namespace {
QString quote(const Path& p){auto s=QString::fromStdString(p.string());s.replace("'","'\\''");return "'"+s+"'";}
QString loader(const Path& sys){return QString("%1 --library-path %2").arg(quote(sys/"usr/lib/ld-linux-aarch64.so.1"),quote(sys/"usr/lib"));}
void wrapper(const Path& app,const QString& body){underlay::atomicFile(app/"bin/reader-experience1-prepare",("#!/bin/sh\n"+body).toUtf8());CHECK(chmod((app/"bin/reader-experience1-prepare").c_str(),0700)==0);}
QByteArray hash(const std::vector<std::uint32_t>& v){return underlay::digest(QByteArray(reinterpret_cast<const char*>(v.data()),qsizetype(v.size()*4))).toHex();}
underlay::Prepared oneShot(const Path& app,const Path& document,int page){
 QProcess p;p.start(QString::fromStdString((app/"bin/reader-experience1-prepare").string()),{"--page",QString::fromStdString(document.string()),QString::number(page),QString::fromStdString(app.string())});
 CHECK(p.waitForFinished(60000)&&p.exitStatus()==QProcess::NormalExit&&p.exitCode()==0);auto file=reader::pagePath(document,page)/"prepared.bin";auto out=underlay::loadPrepared(file);std::filesystem::remove(file);return out;
}
void same(const underlay::Prepared& a,const underlay::Prepared& b){CHECK(a.binding==b.binding&&a.metadata==b.metadata&&hash(a.background)==hash(b.background)&&hash(a.ink)==hash(b.ink)&&a.lastId==b.lastId);
 CHECK(bool(a.text)==bool(b.text));if(a.text&&b.text)CHECK(a.text->data==b.text->data);}
}
int main(int argc,char** argv){QCoreApplication application(argc,argv);if(argc!=5)return 64;
 try{
  const Path source=argv[1],parent=argv[2],build=argv[3],sys=argv[4];underlay::privateDirectory(parent);
  const auto app=parent/"application";underlay::privateDirectory(app/"bin");std::filesystem::copy_file(build/"reader-experience1-prepare",app/"bin/helper-real");
  const QString real=QString("exec %1 %2 \"$@\"\n").arg(loader(sys),quote(app/"bin/helper-real"));wrapper(app,real);
  const auto root=parent/"library";underlay::privateDirectory(root/"inbox");std::filesystem::copy_file(source/"fixtures/library/authored-three-chapters.epub",root/"inbox/authored.epub");
  std::atomic<bool> stop{false};reader::syncLibrary(app,root,stop);auto books=library::documents(root);CHECK(books.size()==1&&books[0].format=="epub"&&books[0].pages==3);
  const auto document=library::documentRoot(root,books[0].id);const auto copy=parent/"one-shot";std::filesystem::copy(root,copy,std::filesystem::copy_options::recursive);const auto twin=library::documentRoot(copy,books[0].id);
  const auto before=reader::pageServerStats();unsigned long long times[3]{};
  for(int i:{1,2,0}){auto t0=std::chrono::steady_clock::now();auto served=reader::preparePage(app,document,i,stop);times[i]=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t0).count();same(served,oneShot(app,twin,i));}
  auto stats=reader::pageServerStats();CHECK(stats.spawned==before.spawned+1&&stats.served==before.served+3&&stats.fallbacks==before.fallbacks&&stats.failures==before.failures);
  reader::closePageServer();int status{};CHECK(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD); /* the server was reaped */
  {auto again=reader::preparePage(app,document,1,stop);same(again,oneShot(app,twin,1));CHECK(reader::pageServerStats().spawned==stats.spawned+1);reader::closePageServer();}
  /* a helper that does not serve (no READY): refused once, then one-shot helpers give the same pages */
  wrapper(app,"if [ \"$1\" = --serve ]; then exec /bin/sleep 30; fi\n"+real);
  {auto t=reader::pageServerStats();auto page=reader::preparePage(app,document,2,stop);same(page,oneShot(app,twin,2));auto u=reader::pageServerStats();CHECK(u.fallbacks==t.fallbacks+1&&u.spawned==t.spawned);
   auto next=reader::preparePage(app,document,0,stop);same(next,oneShot(app,twin,0));CHECK(reader::pageServerStats().fallbacks==t.fallbacks+2);} /* not retried for the same binary */
  reader::closePageServer();
  std::printf("PAGE_SERVER_PASS identical_to_one_shot=3 one_process=true reaped=true respawn=true refused_fallback=true first_us=%llu next_us=%llu,%llu\n",times[1],times[2],times[0]);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"PAGE_SERVER_FAILED %s\n",e.what());return 1;}
}
