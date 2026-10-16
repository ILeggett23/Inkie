#include "touch.h"
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <time.h>
namespace reader {
template<std::size_t N>bool bit(const std::array<unsigned long,N>& bits,unsigned b){return b/(8*sizeof(long))<N&&(bits[b/(8*sizeof(long))]&(1UL<<(b%(8*sizeof(long)))));}
Touch::~Touch(){if(fd_>=0)close(fd_);}
bool Touch::open(bool allowed)noexcept{
 if(!allowed||fd_>=0)return false;DIR* d=opendir("/dev/input");if(!d)return false;bool fail=false;unsigned seen=0,count=0;
 while(auto* e=readdir(d)){if(++count>128){fail=true;break;}if(strncmp(e->d_name,"event",5))continue;bool numeric=e->d_name[5];for(auto* p=e->d_name+5;*p;++p)if(*p<'0'||*p>'9')numeric=false;if(!numeric)continue;
  int f=openat(dirfd(d),e->d_name,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);if(f<0){fail=true;break;}char name[128]{};input_id id{};std::array<unsigned long,16> key{},abs{};input_absinfo x{},y{},slots{};
  bool match=ioctl(f,EVIOCGNAME(sizeof name),name)>=0&&strcmp(name,"Elan touch input")==0&&ioctl(f,EVIOCGID,&id)>=0&&id.bustype==BUS_SPI;
  if(!match){close(f);continue;}
  if(++seen!=1||ioctl(f,EVIOCGBIT(EV_KEY,sizeof key),key.data())<0||ioctl(f,EVIOCGBIT(EV_ABS,sizeof abs),abs.data())<0||bit(key,BTN_TOOL_PEN)||bit(key,BTN_TOOL_RUBBER)||!bit(abs,ABS_MT_SLOT)||!bit(abs,ABS_MT_TRACKING_ID)||!bit(abs,ABS_MT_POSITION_X)||!bit(abs,ABS_MT_POSITION_Y)||ioctl(f,EVIOCGABS(ABS_MT_POSITION_X),&x)<0||ioctl(f,EVIOCGABS(ABS_MT_POSITION_Y),&y)<0||ioctl(f,EVIOCGABS(ABS_MT_SLOT),&slots)<0||x.maximum<=x.minimum||y.maximum<=y.minimum||slots.minimum!=0||slots.maximum>=16){close(f);fail=true;break;}
  struct stat st{};int clock=CLOCK_MONOTONIC;if(fstat(f,&st)||!S_ISCHR(st.st_mode)||x.minimum< -1000000||x.maximum>1000000||y.minimum< -1000000||y.maximum>1000000||ioctl(f,EVIOCSCLOCKID,&clock)<0){close(f);fail=true;break;}
  fd_=f;x_={x.minimum,x.maximum};y_={y.minimum,y.maximum};
 }
 closedir(d);if(fail||seen!=1){if(fd_>=0)close(fd_);fd_=-1;return false;}return true;
}
Action Touch::feed(const input_event& e,bool busy,spike::Ns now)noexcept{
 if(x_.max<=x_.min||y_.max<=y_.min)return Action::None;
 if(busy)blocked_=true;
 if(e.type==EV_SYN&&e.code==SYN_DROPPED){blocked_=true;bad_=true;return Action::None;}
 if(e.type==EV_ABS){if(e.code==ABS_MT_SLOT){if(e.value<0||e.value>=16)bad_=true;else slot_=e.value;}
  else if(e.code==ABS_MT_TRACKING_ID){if(e.value< -1)bad_=true;ids_[slot_]=e.value;}
  else if(e.code==ABS_MT_POSITION_X){if(e.value<x_.min||e.value>x_.max)bad_=true;else lastX_=int((std::int64_t(e.value)-x_.min)*1619/(x_.max-x_.min));}
  else if(e.code==ABS_MT_POSITION_Y){if(e.value<y_.min||e.value>y_.max)bad_=true;else lastY_=int((std::int64_t(e.value)-y_.min)*2159/(y_.max-y_.min));}
 }
 if(e.type!=EV_SYN||e.code!=SYN_REPORT)return Action::None;unsigned touches=0;for(int id:ids_)if(id>=0)++touches;
 if(touches>1)blocked_=true;if(touches&&!gesture_){gesture_=true;start_=now;startX_=lastX_;startY_=lastY_;}
 if(gesture_&&(std::abs(lastX_-startX_)>25||std::abs(lastY_-startY_)>25))blocked_=true;
 if(!touches){Action a=Action::None;if(gesture_&&!busy&&!blocked_&&!bad_&&now>=start_&&now-start_<=600000000ULL){lastTap_={true,lastX_,lastY_};a=hit(lastX_,lastY_);}gesture_=blocked_=bad_=false;return a;}return Action::None;
}
Action Touch::poll(bool busy)noexcept{
 if(fd_<0)return Action::None;std::array<input_event,64> events{};Action result=Action::None;
 for(unsigned batches=0;batches<4;++batches){auto n=read(fd_,events.data(),sizeof events);if(n<0){if(errno==EAGAIN||errno==EINTR)break;bad_=true;break;}if(!n||n%sizeof(input_event)){bad_=true;break;}for(std::size_t i=0;i<std::size_t(n)/sizeof(input_event);++i){const auto& e=events[i];if(e.time.tv_sec<0||e.time.tv_usec<0||e.time.tv_usec>=1000000){bad_=true;continue;}auto a=feed(e,busy,spike::Ns(e.time.tv_sec)*1000000000ULL+spike::Ns(e.time.tv_usec)*1000ULL);if(a!=Action::None)result=a;}}return result;
}
}
