#pragma once
#include "ux_shell.h"
#include "touch.h"
namespace ux {
class Display final:public spike::SessionDisplay,public spike::OperationSink {
 spike::VendorDisplay& vendor_;Shell& shell_;reader::Touch touch_;bool drainTouch_{};
 void compose(spike::Rect r){underlay::compose(vendor_.surface(),shell_.background(),surface(),r);}
public:
 Display(spike::VendorDisplay& v,Shell& s):vendor_(v),shell_(s){}
 bool initialize(const spike::RecoveryLease& l)noexcept override{return vendor_.initialize(l)&&touch_.open(l.valid());}
 spike::Surface surface()const noexcept override{return shell_.ink();}
 bool initialPage()noexcept override{try{compose({0,0,1620,2160});return vendor_.initialPreparedPage(vendor_.surface().pixels);}catch(...){return false;}}
 spike::Submit submit(spike::Rect r)noexcept override{try{compose(r);return vendor_.submit(r);}catch(...){return spike::Submit::Failed;}}
 void pump()noexcept override{vendor_.pump();}void shutdown()noexcept override{vendor_.shutdown();}
 bool diagnosticSegment()const noexcept override{return false;}spike::OperationSink* operationSink()noexcept override{return this;}
 bool reserve()noexcept override{touch_.block();drainTouch_=true;return shell_.reserve();}
 bool append(const spike::Point& p)noexcept override{return shell_.append(p);}void cancel()noexcept override{shell_.cancel();}
 bool admissionOpen()const noexcept override{return shell_.writable();}std::uint64_t pageVersion()const noexcept override{return shell_.version();}
 void pageInstalled()noexcept override{shell_.installed();}
 bool readerBoundary()noexcept override{try{if(drainTouch_){touch_.quiesce();drainTouch_=false;}auto tap=touch_.pollTap(false);if(tap.valid)shell_.action(hit(shell_.state(),tap.x,tap.y));shell_.tick();if(shell_.exiting())return false;auto change=shell_.takeChange();
  if(change.full){compose({0,0,1620,2160});if(!vendor_.readerPage()){shell_.fail();return false;}}
  else for(unsigned i=0;i<Regions.size();++i)if(change.regions&(1u<<i)){compose(Regions[i]);if(vendor_.submit(Regions[i])!=spike::Submit::Accepted){shell_.fail();return false;}}
  return true;
 }catch(...){shell_.fail();return false;}}
};
}
