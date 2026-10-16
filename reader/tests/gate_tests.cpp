#include "session.h"
#include <vector>
#include <memory>
#include <cstdio>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){try{using namespace spike;std::vector<std::uint32_t> pixels(10000,~0u);FakeDisplay display;auto engine=std::make_unique<Engine>(Transform{{0,99},{0,99},100,100,0,false},Surface{pixels,100,100,100},display);FrameAssembler assembly;Scheduler scheduler;SessionStats stats;InputGate gate;gate.notBefore=2000000000;
 auto event=[](int type,int code,int value,int sec){input_event e{};e.type=type;e.code=code;e.value=value;e.time.tv_sec=sec;return e;};
 auto down=[&](int sec){std::array ev={event(EV_KEY,BTN_TOOL_PEN,1,sec),event(EV_ABS,ABS_X,10,sec),event(EV_ABS,ABS_Y,10,sec),event(EV_KEY,BTN_TOUCH,1,sec),event(EV_SYN,SYN_REPORT,0,sec)};CHECK(consumeEvents(ev,assembly,*engine,scheduler,stats,3000000000,nullptr,nullptr,&gate));};
 auto up=[&](int sec){std::array ev={event(EV_KEY,BTN_TOUCH,0,sec),event(EV_SYN,SYN_REPORT,0,sec)};CHECK(consumeEvents(ev,assembly,*engine,scheduler,stats,3000000001,nullptr,nullptr,&gate));};
 down(1);up(3);CHECK(stats.samples==0&&gate.rejected==2&&!gate.blocked);gate.open=false;down(3);gate.open=true;up(3);CHECK(stats.samples==0&&gate.rejected==4);down(3);up(3);CHECK(stats.samples==2&&stats.contacts==1&&stats.released==1);std::puts("PAGE_EPOCH_GATE_PASS stale_contacts_not_admitted=2 fresh_contact=1 accepted_samples=2");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"GATE_FAIL %s\n",e.what());return 1;}}
