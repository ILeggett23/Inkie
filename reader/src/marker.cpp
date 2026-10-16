// SPDX-License-Identifier: AGPL-3.0-only
#include "marker.h"
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <time.h>
namespace spike {
bool MarkerMetadata::valid()const noexcept {
 auto axis=[](Axis a){return a.min>=-1000000&&a.max<=1000000&&a.max>a.min;};
 return name.back()==0&&std::strcmp(name.data(),"Elan marker input")==0&&bus==BUS_SPI&&pen&&rubber&&touch&&absX&&absY&&axis(x)&&axis(y)&&resolutionX>=0&&resolutionY>=0&&(!pressure||(p.min>=0&&p.max>p.min&&p.max<=65535));
}
FrameResult FrameAssembler::fail()noexcept{fault_=true;contact_=contactRubber_=touch_=pen_=rubber_=changed_=false;pressure_=0;return FrameResult::Fault;}
FrameResult FrameAssembler::feed(unsigned type,unsigned code,int value,Ns now,Sample& out)noexcept{
 if(fault_)return FrameResult::Fault;
 if(type==EV_SYN&&code==SYN_DROPPED){++evidence_.drops;return fail();}
 if(type==EV_ABS){
  if(code==ABS_X){x_=value;haveX_=changed_=true;}
  if(code==ABS_Y){y_=value;haveY_=changed_=true;}
  if(code==ABS_PRESSURE){if(!pressureAvailable_||value<pressureAxis_.min||value>pressureAxis_.max)return fail();pressure_=value;havePressure_=changed_=true;}
 }
 if(type==EV_KEY&&(code==BTN_TOOL_PEN||code==BTN_TOOL_RUBBER||code==BTN_TOUCH)){
  if(value!=0&&value!=1)return fail();
  if(code==BTN_TOOL_PEN)pen_=value;
  if(code==BTN_TOOL_RUBBER)rubber_=value;
  if(code==BTN_TOUCH)touch_=value;
  changed_=true;
 }
 if(type!=EV_SYN||code!=SYN_REPORT||!changed_)return FrameResult::None;
 changed_=false;++evidence_.frames;
 if(rubber_)++evidence_.rubberFrames;
 if(havePressure_){if(!evidence_.pressureFrames)evidence_.min=evidence_.max=pressure_;else{if(pressure_<evidence_.min)evidence_.min=pressure_;if(pressure_>evidence_.max)evidence_.max=pressure_;}++evidence_.pressureFrames;}
 if(pen_&&rubber_)return fail();
 bool next=(pen_||rubber_)&&touch_;
 if(contact_&&next&&contactRubber_!=rubber_)return fail();
 if(!next&&!contact_)return FrameResult::None;
 if(!now||!haveX_||!haveY_)return fail();
 out={x_,y_,next?(contact_?Phase::Move:Phase::Down):Phase::Up,now,pressure_,havePressure_,next?rubber_:contactRubber_};contact_=next;if(next)contactRubber_=rubber_;
 return FrameResult::Ready;
}
bool validEventBytes(std::size_t n)noexcept{return n>0&&n<=64*sizeof(input_event)&&n%sizeof(input_event)==0;}
template<std::size_t N> static bool bit(const std::array<unsigned long,N>&a,unsigned b){return b/(8*sizeof(long))<N&&(a[b/(8*sizeof(long))]&(1UL<<(b%(8*sizeof(long)))));}
bool Marker::openExclusive(bool exclusive)noexcept{
 if(!exclusive||fd>=0)return false;
 DIR*d=opendir("/dev/input");if(!d)return false;
 unsigned entries=0,matches=0;bool bad=false;
 while(auto* ent=readdir(d)){
  if(++entries>128){bad=true;break;}
  if(std::strncmp(ent->d_name,"event",5))continue;
  const char*q=ent->d_name+5;if(!*q)continue;bool numeric=true;for(;*q;++q)if(*q<'0'||*q>'9')numeric=false;if(!numeric)continue;
  int f=openat(dirfd(d),ent->d_name,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);if(f<0){bad=true;break;}
  struct stat st{};input_id id{};MarkerMetadata m;
  std::array<unsigned long,8> ev{},ab{};std::array<unsigned long,16> key{};
  if(fstat(f,&st)||!S_ISCHR(st.st_mode)||ioctl(f,EVIOCGID,&id)<0||ioctl(f,EVIOCGNAME(m.name.size()),m.name.data())<0||ioctl(f,EVIOCGBIT(0,sizeof(ev)),ev.data())<0){::close(f);bad=true;break;}
  if(!bit(ev,EV_SYN)||!bit(ev,EV_ABS)||!bit(ev,EV_KEY)){::close(f);continue;}
  if(ioctl(f,EVIOCGBIT(EV_KEY,sizeof(key)),key.data())<0||ioctl(f,EVIOCGBIT(EV_ABS,sizeof(ab)),ab.data())<0){::close(f);bad=true;break;}
  m.bus=id.bustype;m.pen=bit(key,BTN_TOOL_PEN);m.rubber=bit(key,BTN_TOOL_RUBBER);m.touch=bit(key,BTN_TOUCH);m.absX=bit(ab,ABS_X);m.absY=bit(ab,ABS_Y);m.pressure=bit(ab,ABS_PRESSURE);
  if(!m.pen||!m.touch||!m.absX||!m.absY){::close(f);continue;}
  input_absinfo x{},y{},p{};
  if(ioctl(f,EVIOCGABS(ABS_X),&x)<0||ioctl(f,EVIOCGABS(ABS_Y),&y)<0||(m.pressure&&ioctl(f,EVIOCGABS(ABS_PRESSURE),&p)<0)){::close(f);bad=true;break;}
  m.x={x.minimum,x.maximum};m.y={y.minimum,y.maximum};m.p={p.minimum,p.maximum};m.resolutionX=x.resolution;m.resolutionY=y.resolution;
  if(!m.valid()||++matches!=1){::close(f);bad=true;break;}
  fd=f;metadata=m;
 }
 if(closedir(d))bad=true;
 if(bad||matches!=1){close();return false;}
 int clock=CLOCK_MONOTONIC;
 if(ioctl(fd,EVIOCSCLOCKID,&clock)<0||ioctl(fd,EVIOCGRAB,1)<0){close();return false;}
 grabbed=true;return true;
}
void Marker::close()noexcept{if(fd>=0){if(grabbed)(void)ioctl(fd,EVIOCGRAB,0);::close(fd);}fd=-1;grabbed=false;}
}
