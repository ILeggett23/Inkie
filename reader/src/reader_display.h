#pragma once
#include "reader.h"
#include "touch.h"
namespace reader {
class ReaderDisplay final:public spike::SessionDisplay,public spike::OperationSink {
 spike::VendorDisplay& vendor_;Reader& reader_;Touch touch_;bool drainTouch_{},loading_{};
 void composite(){auto& p=reader_.active().prepared;underlay::compose(vendor_.surface(),p.background,surface(),{0,0,underlay::Width,underlay::Height});}
public:
 ReaderDisplay(spike::VendorDisplay& vendor,Reader& reader):vendor_(vendor),reader_(reader){}
 bool initialize(const spike::RecoveryLease& l)noexcept override{return vendor_.initialize(l)&&touch_.open(l.valid());}
 spike::Surface surface()const noexcept override{return {reader_.active().prepared.ink,underlay::Width,underlay::Height,underlay::Stride};}
 bool initialPage()noexcept override{try{composite();if(!vendor_.initialPreparedPage(vendor_.surface().pixels))return false;reader_.activate();return true;}catch(...){return false;}}
 spike::Submit submit(spike::Rect r)noexcept override{try{underlay::compose(vendor_.surface(),reader_.active().prepared.background,surface(),r);return vendor_.submit(r);}catch(...){return spike::Submit::Failed;}}
 void pump()noexcept override{vendor_.pump();}void shutdown()noexcept override{vendor_.shutdown();}
 bool diagnosticSegment()const noexcept override{return false;}spike::OperationSink* operationSink()noexcept override{return this;}
 bool reserve()noexcept override{touch_.block();drainTouch_=true;return reader_.reserve();}
 bool append(const spike::Point& p)noexcept override{return reader_.append(p);}void cancel()noexcept override{reader_.cancel();}
 bool admissionOpen()const noexcept override{return reader_.inkAdmissible();}std::uint64_t pageVersion()const noexcept override{return reader_.view();}
 void pageInstalled()noexcept override{reader_.installed();reader_.activate();}
 bool readerBoundary()noexcept override{
  try{if(drainTouch_){touch_.quiesce();drainTouch_=false;}auto a=touch_.poll(false);if(a!=Action::None)reader_.request(a);if(reader_.exiting())return false;
   bool changed=reader_.tick();if(changed){composite();if(!vendor_.readerPage(ux::FullRefreshReason::DocumentBinding)){reader_.fail();return false;}loading_=false;}
   else if(reader_.state()==State::Preparing&&!loading_){chrome(vendor_.surface(),reader_.page(),reader_.total(),true);if(vendor_.submit({0,underlay::Height-100,underlay::Width,underlay::Height})!=spike::Submit::Accepted){reader_.fail();return false;}loading_=true;}
   else if(loading_&&reader_.state()==State::Active){auto r=spike::Rect{0,underlay::Height-100,underlay::Width,underlay::Height};underlay::compose(vendor_.surface(),reader_.active().prepared.background,surface(),r);if(vendor_.submit(r)!=spike::Submit::Accepted){reader_.fail();return false;}loading_=false;}
   return true;
  }catch(...){reader_.fail();return false;}
 }
};
}
