// SPDX-License-Identifier: AGPL-3.0-only
#include "page_thumbnails.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace thumbs {namespace {
struct Fd{int n{-1};~Fd(){if(n>=0)::close(n);}void reset(){if(n>=0)::close(n);n=-1;}};
void put32(QByteArray& b,std::uint32_t v){for(int i=0;i<4;++i)b.append(char((v>>(8*i))&0xff));}
void put16(QByteArray& b,std::uint32_t v){for(int i=0;i<2;++i)b.append(char((v>>(8*i))&0xff));}
std::uint32_t get(const QByteArray& b,qsizetype at,int bytes){std::uint32_t v=0;for(int i=0;i<bytes;++i)v|=std::uint32_t(std::uint8_t(b[at+i]))<<(8*i);return v;}
std::pair<int,int> fit(int width,int height){
 const double s=std::min({double(MaxWidth)/width,double(MaxHeight)/height,1.0});
 return {std::clamp(int(std::lround(width*s)),1,MaxWidth),std::clamp(int(std::lround(height*s)),1,MaxHeight)};}
// Box filter over the source block each target pixel covers; luma from 8-bit RGB.
template<class Luma>Image reduce(int page,int width,int height,Luma luma){
 if(width<1||height<1)throw std::runtime_error("thumbnail-source-size");auto [w,h]=fit(width,height);Image out{page,w,h,std::vector<std::uint8_t>(std::size_t(w)*h)};
 for(int ty=0;ty<h;++ty){const int y0=int(std::int64_t(ty)*height/h),y1=std::max(y0+1,int(std::int64_t(ty+1)*height/h));
  for(int tx=0;tx<w;++tx){const int x0=int(std::int64_t(tx)*width/w),x1=std::max(x0+1,int(std::int64_t(tx+1)*width/w));std::uint64_t sum=0;
   for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)sum+=luma(x,y);out.gray[std::size_t(ty)*w+tx]=std::uint8_t(sum/std::uint64_t((y1-y0)*(x1-x0)));}}
 return out;}
std::uint32_t lumaOf(std::uint32_t r,std::uint32_t g,std::uint32_t b){return (r*299+g*587+b*114)/1000;}
}
QByteArray encode(const std::vector<Image>& images){
 if(images.size()>std::size_t(MaxBatch))throw std::runtime_error("thumbnail-batch");QByteArray out("IBTHUMB1");put32(out,std::uint32_t(images.size()));
 for(const auto& i:images){if(i.page<0||i.width<1||i.height<1||i.width>MaxWidth||i.height>MaxHeight||i.gray.size()!=std::size_t(i.width)*i.height)throw std::runtime_error("thumbnail-image");
  put32(out,std::uint32_t(i.page));put16(out,std::uint32_t(i.width));put16(out,std::uint32_t(i.height));out.append(reinterpret_cast<const char*>(i.gray.data()),qsizetype(i.gray.size()));}
 return out;}
std::optional<std::vector<Image>> decode(const QByteArray& b,int first,int count){
 if(count<1||count>MaxBatch||first<0||std::size_t(b.size())>MaxWireBytes||b.size()<12||!b.startsWith("IBTHUMB1")||get(b,8,4)!=std::uint32_t(count))return std::nullopt;
 std::vector<Image> out;qsizetype at=12;
 for(int n=0;n<count;++n){if(b.size()-at<8)return std::nullopt;Image i{int(get(b,at,4)),int(get(b,at+4,2)),int(get(b,at+6,2)),{}};at+=8;
  if(i.page!=first+n||i.width<1||i.height<1||i.width>MaxWidth||i.height>MaxHeight)return std::nullopt;const auto bytes=qsizetype(i.width)*i.height;if(b.size()-at<bytes)return std::nullopt;
  i.gray.assign(reinterpret_cast<const std::uint8_t*>(b.constData()+at),reinterpret_cast<const std::uint8_t*>(b.constData()+at+bytes));at+=bytes;out.push_back(std::move(i));}
 if(at!=b.size())return std::nullopt;return out;}
Image fromRgb32(int page,const std::uint32_t* p,int width,int height,int stride){
 if(!p||stride<width)throw std::runtime_error("thumbnail-source-stride");
 return reduce(page,width,height,[&](int x,int y){auto v=p[std::size_t(y)*stride+x];return lumaOf((v>>16)&0xff,(v>>8)&0xff,v&0xff);});}
Image fromRgba(int page,const std::uint8_t* p,int width,int height,int strideBytes){
 if(!p||strideBytes<width*4)throw std::runtime_error("thumbnail-source-stride");
 return reduce(page,width,height,[&](int x,int y){auto q=p+std::size_t(y)*strideBytes+std::size_t(x)*4;return lumaOf(q[0],q[1],q[2]);});}
Service::~Service(){cancel();if(worker_.joinable())worker_.join();}
void Service::cancel()noexcept{if(worker_.joinable())stop_.store(true);}
void Service::reset()noexcept{cancel();key_={};cache_.clear();refused_.clear();}
bool Service::request(const Key& key,int first,int count){
 if(key.pages<1||key.app.empty()||key.data.empty()||count<1)return false;
 if(!(key==key_)){reset();key_=key;}
 first=std::clamp(first,0,key.pages-1);count=std::min({count,MaxBatch,key.pages-first});
 bool missing=false;for(int p=first;p<first+count;++p)missing|=!cache_.contains(p);
 if(worker_.joinable()){if(!(running_==key)||first_!=first)cancel();return false;}
 if(!missing||refused_.contains(first))return false;
 stop_.store(false);done_.store(false);first_=first;count_=count;running_=key;result_.reset();
 worker_=std::thread([this,key,first,count]{result_=run(key,first,count,stop_);done_.store(true,std::memory_order_release);});
 return true;}
bool Service::collect(){
 if(!worker_.joinable()||!done_.load(std::memory_order_acquire))return false;worker_.join();bool fresh=false;
 if(running_==key_&&!stop_.load()){
  if(result_){for(auto& i:*result_)cache_[i.page]=std::move(i);fresh=true;
   // Keep the pages nearest the batch just shown; a thumbnail is cheap to ask for again.
   const int centre=first_+count_/2;while(cache_.size()>CacheLimit){auto far=cache_.begin();for(auto i=cache_.begin();i!=cache_.end();++i)if(std::abs(i->first-centre)>std::abs(far->first-centre))far=i;cache_.erase(far);}}
  else{refused_.insert(first_);++refusals_;}}
 result_.reset();stop_.store(false);return fresh;}
std::optional<std::vector<Image>> Service::run(const Key& key,int first,int count,const std::atomic<bool>& stop){
 try{
  auto helper=(key.app/"bin/reader-experience1-prepare").string();struct stat st{};
  if(lstat(helper.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0022)||!key.data.is_absolute())return std::nullopt;
  int fds[2];if(pipe2(fds,O_CLOEXEC))return std::nullopt;Fd in{fds[0]},out{fds[1]};
  auto path=key.data.string(),view=(key.view.isEmpty()?QString("baseline"):key.view).toStdString(),from=std::to_string(first),n=std::to_string(count);
  char envPath[]="PATH=/usr/bin:/bin",lang[]="LANG=C.UTF-8";char* env[]={envPath,lang,nullptr};
  char* args[]={helper.data(),const_cast<char*>("--thumbnails"),path.data(),view.data(),from.data(),n.data(),nullptr};
  // The pipe becomes the helper's stdout; every other descriptor (vendor, input, owner sockets) stays closed.
  posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions))return std::nullopt;
  int setup=posix_spawn_file_actions_adddup2(&actions,out.n,1);if(!setup)setup=posix_spawn_file_actions_addclosefrom_np(&actions,3);
  pid_t pid{};int spawned=setup?setup:posix_spawn(&pid,helper.c_str(),&actions,nullptr,args,env);posix_spawn_file_actions_destroy(&actions);out.reset();if(spawned)return std::nullopt;
  if(fcntl(in.n,F_SETFL,fcntl(in.n,F_GETFL)|O_NONBLOCK)){kill(pid,SIGKILL);int s{};while(waitpid(pid,&s,0)<0&&errno==EINTR){}return std::nullopt;}
  QByteArray wire;char buffer[65536];bool eof=false,exited=false,term=false,killed=false,overflow=false;int status{};
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);
  while(!(eof&&exited)){
   if(!eof){pollfd p{in.n,POLLIN,0};(void)poll(&p,1,5);
    for(;;){auto got=::read(in.n,buffer,sizeof buffer);if(got>0){if(std::size_t(wire.size())+std::size_t(got)>MaxWireBytes){overflow=true;break;}wire.append(buffer,qsizetype(got));continue;}
     if(got==0){eof=true;break;}if(errno==EINTR)continue;if(errno!=EAGAIN&&errno!=EWOULDBLOCK)eof=true;break;}}
   else std::this_thread::sleep_for(std::chrono::milliseconds(1));
   if(!exited){auto w=waitpid(pid,&status,WNOHANG);if(w==pid)exited=true;else if(w<0&&errno!=EINTR){exited=true;status=-1;}}
   auto now=std::chrono::steady_clock::now();
   if(!exited&&!term&&(stop.load()||overflow||now>=deadline)){kill(pid,SIGTERM);term=true;deadline=now+std::chrono::milliseconds(20);}
   if(!exited&&term&&!killed&&now>=deadline){kill(pid,SIGKILL);killed=true;}
   if(overflow)eof=true;
  }
  if(term||overflow||stop.load()||!WIFEXITED(status)||WEXITSTATUS(status))return std::nullopt;
  return decode(wire,first,count);
 }catch(...){return std::nullopt;}
}
}
