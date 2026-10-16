#include "touch.h"
#include <cmath>
#include <algorithm>
#include "gesture_policy.h"
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
void Touch::cancelLongPress()noexcept{if(longFired_&&!cancellationSent_){lastTap_={true,0,0,false,true};cancellationSent_=true;}}
bool Touch::snapshotSlots(bool recover)noexcept{if(fd_<0)return false;std::array<int,17> xs{},ys{},types{},ids{};auto query=[&](int code,std::array<int,17>& out){out[0]=code;return ioctl(fd_,EVIOCGMTSLOTS(sizeof(out)),out.data())>=0;};bool xok=query(ABS_MT_POSITION_X,xs),yok=query(ABS_MT_POSITION_Y,ys),tok=query(ABS_MT_TOOL_TYPE,types),iok=!recover||query(ABS_MT_TRACKING_ID,ids);if(recover&&(!xok||!yok||!iok))return false;input_absinfo current{};if(recover&&(ioctl(fd_,EVIOCGABS(ABS_MT_SLOT),&current)<0||current.value<0||current.value>=slotCount_))return false;
 for(int f=0;f<slotCount_;++f){if(xok&&xs[f+1]>=x_.min&&xs[f+1]<=x_.max){fingerX_[f]=int((std::int64_t(xs[f+1])-x_.min)*1619/(x_.max-x_.min));knownX_[f]=true;}if(yok&&ys[f+1]>=y_.min&&ys[f+1]<=y_.max){fingerY_[f]=int((std::int64_t(ys[f+1])-y_.min)*2159/(y_.max-y_.min));knownY_[f]=true;}if(tok&&types[f+1]>=0&&types[f+1]<=MT_TOOL_PALM)tools_[f]=types[f+1];if(recover){if(ids[f+1]<-1)return false;ids_[f]=ids[f+1];originX_[f]=originY_[f]=-1;}}
 if(recover){slot_=current.value;gesture_=longFired_=longCancelled_=cancellationSent_=nonFingerChanged_=false;bool active=false;for(int f=0;f<slotCount_;++f)active|=ids_[f]>=0&&tools_[f]==MT_TOOL_FINGER;blocked_=penBlocked_=active;bad_=false;lost_=false;}return xok&&yok;
}
bool Touch::open(bool allowed)noexcept{
 if(!allowed||fd_>=0)return false;DIR* d=opendir("/dev/input");if(!d)return false;bool fail=false;unsigned seen=0,count=0;
 while(auto* e=readdir(d)){if(++count>128){fail=true;break;}if(strncmp(e->d_name,"event",5))continue;bool numeric=e->d_name[5];for(auto* p=e->d_name+5;*p;++p)if(*p<'0'||*p>'9')numeric=false;if(!numeric)continue;
  int f=openat(dirfd(d),e->d_name,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);if(f<0){fail=true;break;}char name[128]{};input_id id{};std::array<unsigned long,16> key{},abs{};input_absinfo x{},y{},slots{};
  bool match=ioctl(f,EVIOCGNAME(sizeof name),name)>=0&&strcmp(name,"Elan touch input")==0&&ioctl(f,EVIOCGID,&id)>=0&&id.bustype==BUS_SPI;
  if(!match){close(f);continue;}
  if(++seen!=1||ioctl(f,EVIOCGBIT(EV_KEY,sizeof key),key.data())<0||ioctl(f,EVIOCGBIT(EV_ABS,sizeof abs),abs.data())<0||bit(key,BTN_TOOL_PEN)||bit(key,BTN_TOOL_RUBBER)||!bit(abs,ABS_MT_SLOT)||!bit(abs,ABS_MT_TRACKING_ID)||!bit(abs,ABS_MT_POSITION_X)||!bit(abs,ABS_MT_POSITION_Y)||ioctl(f,EVIOCGABS(ABS_MT_POSITION_X),&x)<0||ioctl(f,EVIOCGABS(ABS_MT_POSITION_Y),&y)<0||ioctl(f,EVIOCGABS(ABS_MT_SLOT),&slots)<0||x.maximum<=x.minimum||y.maximum<=y.minimum||slots.minimum!=0||slots.maximum<0||slots.maximum>=16){close(f);fail=true;break;}
  struct stat st{};int clock=CLOCK_MONOTONIC;if(fstat(f,&st)||!S_ISCHR(st.st_mode)||x.minimum< -1000000||x.maximum>1000000||y.minimum< -1000000||y.maximum>1000000||ioctl(f,EVIOCSCLOCKID,&clock)<0){close(f);fail=true;break;}
  fd_=f;x_={x.minimum,x.maximum};y_={y.minimum,y.maximum};slotCount_=slots.maximum+1;(void)snapshotSlots(false);
 }
 closedir(d);if(fail||seen!=1){if(fd_>=0)close(fd_);fd_=-1;return false;}return true;
}
Action Touch::feed(const input_event& e,bool busy,spike::Ns now)noexcept{
 if(x_.max<=x_.min||y_.max<=y_.min||lost_)return Action::None;

 if(busy){blocked_=true;penBlocked_=true;} /* pen activity anywhere in a touch rules out every gesture */
 if(e.type==EV_SYN&&e.code==SYN_DROPPED){cancelLongPress();blocked_=true;bad_=true;lost_=true;++counters_.losses;return Action::None;}
 if(e.type==EV_ABS){if(e.code==ABS_MT_SLOT){if(e.value<0||e.value>=slotCount_)bad_=true;else slot_=e.value;}
  else if(e.code==ABS_MT_TRACKING_ID){if(e.value< -1)bad_=true;if(gesture_&&e.value>=0&&ids_[slot_]!=e.value&&((usedSlots_>>slot_)&1u)){bad_=blocked_=true;cancelLongPress();}if(ids_[slot_]>=0&&e.value>=0&&ids_[slot_]!=e.value)blocked_=true;if(e.value>=0&&ids_[slot_]!=e.value)originX_[slot_]=originY_[slot_]=-1;ids_[slot_]=e.value;}
  else if(e.code==ABS_MT_TOOL_TYPE){if(e.value<MT_TOOL_FINGER||e.value>MT_TOOL_PALM)bad_=true;else{if(gesture_&&e.value!=MT_TOOL_FINGER&&((usedSlots_>>slot_)&1u)){nonFingerChanged_=blocked_=true;cancelLongPress();}tools_[slot_]=e.value;}}
  else if(e.code==ABS_MT_POSITION_X){if(e.value<x_.min||e.value>x_.max)bad_=true;else{lastX_=int((std::int64_t(e.value)-x_.min)*1619/(x_.max-x_.min));fingerX_[slot_]=lastX_;knownX_[slot_]=true;if(originX_[slot_]<0)originX_[slot_]=lastX_;}}
  else if(e.code==ABS_MT_POSITION_Y){if(e.value<y_.min||e.value>y_.max)bad_=true;else{lastY_=int((std::int64_t(e.value)-y_.min)*2159/(y_.max-y_.min));fingerY_[slot_]=lastY_;knownY_[slot_]=true;if(originY_[slot_]<0)originY_[slot_]=lastY_;}}
 }
 if(e.type!=EV_SYN||e.code!=SYN_REPORT)return Action::None;lastFrame_=now;unsigned touches=0;int only=-1;bool palm=false;for(int f=0;f<slotCount_;++f)if(ids_[f]>=0){palm|=tools_[f]==MT_TOOL_PALM;if(tools_[f]!=MT_TOOL_FINGER)continue;++touches;only=f;if(!knownX_[f]||!knownY_[f])bad_=true;if(originX_[f]<0&&knownX_[f])originX_[f]=fingerX_[f];if(originY_[f]<0&&knownY_[f])originY_[f]=fingerY_[f];}if(palm)++counters_.palmFrames;if(touches==1){lastX_=fingerX_[only];lastY_=fingerY_[only];}
 if(touches&&!gesture_){gesture_=true;intentional_=motionStarted_=false;gestureMode_=mode_;longFired_=longCancelled_=moved_=cancellationSent_=false;maxTouches_=0;travelX_=travelY_=0;start_=now;if(penReleased_&&!busy&&now>penCutoff_){blocked_=false;penBlocked_=false;}startX_=lastX_;startY_=lastY_;toolbarStart_=toolbar_.contains(startX_,startY_);handleDrag_=dragEnabled_&&std::any_of(handles_.begin(),handles_.end(),[&](auto rect){return rect.contains(startX_,startY_);});nonFingerChanged_=false;intentional_=handleDrag_;gestureDrag_=dragEnabled_&&!toolbarStart_&&startY_<2060;gestureEpoch_=epoch_;usedSlots_=0;}
 if(gesture_)for(std::size_t f=0;f<ids_.size();++f)if(ids_[f]>=0&&tools_[f]==MT_TOOL_FINGER){usedSlots_|=1u<<f;}
 if(touches>=2&&maxTouches_<2)multiAt_=now;if(touches>maxTouches_)maxTouches_=touches;if(gesture_)for(std::size_t f=0;f<ids_.size();++f)if(ids_[f]>=0&&tools_[f]==MT_TOOL_FINGER&&originX_[f]>=0&&originY_[f]>=0){travelX_=std::max(travelX_,std::abs(fingerX_[f]-originX_[f]));travelY_=std::max(travelY_,std::abs(fingerY_[f]-originY_[f]));} /* each finger against its own landing point */
 if(touches>1){blocked_=true;cancelLongPress();}
 if(gesture_&&(std::abs(lastX_-startX_)>semantic::LongPressSlop||std::abs(lastY_-startY_)>semantic::LongPressSlop)){if(!motionStarted_){motionStarted_=true;if((gestureMode_==DragMode::Text||gestureMode_==DragMode::Highlight)&&now>=start_&&now-start_>=semantic::LongPressNs&&!busy&&!blocked_&&!bad_)intentional_=true;}longCancelled_=true;if(!(gestureMode_==DragMode::Text&&intentional_))cancelLongPress();}
 if(gesture_&&!gestureDrag_&&(std::abs(lastX_-startX_)>25||std::abs(lastY_-startY_)>25))moved_=true; /* a moving finger is no tap (it may be a swipe) */
 if(!touches){Action a=Action::None;const Gesture g=gesture_?recognise(now,busy):Gesture::None;const bool penRejected=penBlocked_||busy;penBlocked_=false;
  if(g!=Gesture::None){lastTap_={true,lastX_,lastY_};lastTap_.startX=startX_;lastTap_.startY=startY_;lastTap_.epoch=gestureEpoch_;lastTap_.gesture=g;lastTap_.mode=gestureMode_;if(g==Gesture::VerticalScroll){lastTap_.deltaX=lastX_-startX_;lastTap_.deltaY=lastY_-startY_;lastTap_.originY=startY_;}if(Pair p;(g==Gesture::Pinch||g==Gesture::TwoFingerPan)&&pair(p)){lastTap_.scale=p.d1/p.d0;lastTap_.focalX=int(p.cx0);lastTap_.focalY=int(p.cy0);lastTap_.deltaX=int(p.cx1-p.cx0);lastTap_.deltaY=int(p.cy1-p.cy0);lastTap_.originY=int(p.cy0);}gesture_=blocked_=bad_=longFired_=longCancelled_=gestureDrag_=moved_=false;return a;}

  if(gesture_&&moved_)blocked_=true;
  if(gesture_&&gestureDrag_&&longCancelled_&&(gestureMode_!=DragMode::Text||intentional_)&&!busy&&!blocked_&&!bad_&&now>=start_&&now-start_<=15000000000ULL){lastTap_={true,lastX_,lastY_,false,false,true,startX_,startY_,gestureEpoch_};lastTap_.mode=gestureMode_;}else if(gesture_&&!longFired_&&!busy&&!blocked_&&!bad_&&now>=start_&&now-start_<=600000000ULL){lastTap_={true,lastX_,lastY_};a=hit(lastX_,lastY_);}if(gesture_&&!longFired_&&!lastTap_.valid){if(penRejected)++counters_.rejectedPen;else if(bad_||nonFingerChanged_)++counters_.rejectedInvalid;else if(maxTouches_>1)++counters_.rejectedMulti;else ++counters_.rejectedOther;}gesture_=blocked_=bad_=longFired_=longCancelled_=gestureDrag_=moved_=false;return a;}expire(now,busy);return Action::None;
}
bool Touch::pair(Pair& out)const noexcept{int a=-1,b=-1;for(int f=0;f<16;++f)if((usedSlots_>>f)&1u&&originX_[std::size_t(f)]>=0&&originY_[std::size_t(f)]>=0){if(a<0)a=f;else if(b<0)b=f;else return false;}
 if(a<0||b<0)return false;auto at=[&](const std::array<int,16>& v,int f){return float(v[std::size_t(f)]);};
 out.d0=std::hypot(at(originX_,a)-at(originX_,b),at(originY_,a)-at(originY_,b));out.d1=std::hypot(at(fingerX_,a)-at(fingerX_,b),at(fingerY_,a)-at(fingerY_,b));
 out.ax=at(fingerX_,a)-at(originX_,a);out.ay=at(fingerY_,a)-at(originY_,a);out.bx=at(fingerX_,b)-at(originX_,b);out.by=at(fingerY_,b)-at(originY_,b);
 out.cx0=(at(originX_,a)+at(originX_,b))/2;out.cy0=(at(originY_,a)+at(originY_,b))/2;out.cx1=(at(fingerX_,a)+at(fingerX_,b))/2;out.cy1=(at(fingerY_,a)+at(fingerY_,b))/2;return out.d0>0;}
Gesture Touch::recognise(spike::Ns now,bool busy)const noexcept{
 if(busy||penBlocked_||bad_||toolbarStart_||nonFingerChanged_||handleDrag_||longFired_||now<start_)return Gesture::None;const spike::Ns held=now-start_;
 // Several fingers that barely moved and lifted quickly: two undo, three redo. Anything larger (a palm) is ignored.
 if(maxTouches_==2||maxTouches_==3){
  // Undo and redo remove or restore ink, so the fingers must land together (within 150 ms) and, for two, sit a finger
  // pair apart: a finger that is selecting text while a palm or another finger touches down later stays nothing.
  if(held<=400000000ULL&&travelX_<=60&&travelY_<=60){const bool together=multiAt_>=start_&&multiAt_-start_<=150000000ULL;Pair spread;const bool paired=maxTouches_==3||(pair(spread)&&spread.d0>=60&&spread.d0<=700);return together&&paired?(maxTouches_==2?Gesture::Undo:Gesture::Redo):Gesture::None;}
  // Two fingers that moved: a pinch when their spread changed clearly, otherwise a pan when they moved together.
  Pair p;if(maxTouches_!=2||held>5000000000ULL||!pair(p))return Gesture::None;
  // Both fingers must move (one still contact with one moving is a palm, which stays nothing): apart or together
  // for a pinch, in the same direction for a pan.
  if(std::hypot(p.ax,p.ay)<40||std::hypot(p.bx,p.by)<40)return Gesture::None;const float dot=p.ax*p.bx+p.ay*p.by;
  if(dot<0&&p.d0>=60&&std::abs(p.d1-p.d0)>=40&&std::abs(p.d1/p.d0-1.f)>=0.12f)return Gesture::Pinch;
  return dot>0&&std::hypot(p.ax-p.bx,p.ay-p.by)<=0.5f*std::max(std::hypot(p.ax,p.ay),std::hypot(p.bx,p.by))&&std::hypot(p.cx1-p.cx0,p.cy1-p.cy0)>=60?Gesture::TwoFingerPan:Gesture::None;}
 if(statusCorner_&&maxTouches_==1&&startX_>=1080&&startY_<=80&&held<=5000000000ULL&&lastY_-startY_>=250&&std::abs(lastY_-startY_)>=2*std::abs(lastX_-startX_))return Gesture::EdgeDown; /* The device-status corner has the existing bounded pan lifetime; ordinary page/selection deadlines stay unchanged. */
 if(maxTouches_!=1||gestureMode_==DragMode::Ink||gestureMode_==DragMode::Scroll||((gestureMode_==DragMode::Text||gestureMode_==DragMode::Highlight)&&intentional_))return Gesture::None;
 if(gestureMode_==DragMode::Text&&gestureDrag_&&startY_>80&&held<=5000000000ULL&&std::abs(lastY_-startY_)>=60&&std::abs(lastY_-startY_)>2*std::abs(lastX_-startX_))return Gesture::VerticalScroll;
 if(held>800000000ULL)return Gesture::None; /* A page flick keeps the same deadline in and out of text-selection mode. */
 const int dx=lastX_-startX_,dy=lastY_-startY_;
 if(startY_<=80&&dy>=250&&std::abs(dy)>=2*std::abs(dx))return Gesture::EdgeDown;
 if(!dragEnabled_&&startY_>=2160-100&&dy<=-250&&std::abs(dy)>=2*std::abs(dx))return Gesture::EdgeUp; /* not while text selection drags (an edge drag there stays inert) */
 if(std::abs(dx)>=200&&std::abs(dx)>=2*std::abs(dy)&&std::abs(dy)<=200)return dx<0?Gesture::SwipeNext:Gesture::SwipePrevious;
 return Gesture::None;
}
void Touch::expire(spike::Ns now,bool busy)noexcept{unsigned touches=0;for(int f=0;f<slotCount_;++f)touches+=ids_[f]>=0&&tools_[f]==MT_TOOL_FINGER;if(gesture_&&!toolbarStart_&&gestureMode_!=DragMode::Ink&&gestureMode_!=DragMode::Scroll&&!longFired_&&!longCancelled_&&!handleDrag_&&!busy&&!blocked_&&!bad_&&touches==1&&now>=start_&&now-start_>=semantic::LongPressNs){longFired_=true;if(gestureMode_==DragMode::Text)intentional_=true;lastTap_={true,startX_,startY_,true,false};}}
Action Touch::poll(bool busy)noexcept{
 caughtUp_=false;if(fd_<0)return Action::None;Action result=Action::None;
 /* Retain unread events from one bounded read. Stop on the first output so a later contact cannot overwrite it. */
 for(unsigned consumed=0;consumed<256;++consumed){if(stagedAt_==stagedCount_){auto n=read(fd_,staged_.data(),sizeof staged_);if(n<0){if(errno==EAGAIN){caughtUp_=true;break;}if(errno==EINTR)break;bad_=true;break;}if(!n||n%sizeof(input_event)){bad_=true;break;}stagedAt_=0;stagedCount_=std::size_t(n)/sizeof(input_event);lastRead_=spike::monotonic();counters_.readEvents+=stagedCount_;}
  const auto e=staged_[stagedAt_++];++counters_.consumedEvents;if(e.type==EV_SYN&&e.code==SYN_REPORT)++counters_.frames;if(lost_){if(e.type==EV_SYN&&e.code==SYN_REPORT)(void)snapshotSlots(true);continue;}if(e.time.tv_sec<0||e.time.tv_usec<0||e.time.tv_usec>=1000000){bad_=true;continue;}auto action=feed(e,busy,spike::Ns(e.time.tv_sec)*1000000000ULL+spike::Ns(e.time.tv_usec)*1000ULL);if(action!=Action::None)result=action;if(lastTap_.valid)return result;
 }return result;
}
}
