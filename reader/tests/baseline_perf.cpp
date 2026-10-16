#include "underlay.h"
#include <QCoreApplication>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>
thread_local bool measured=false;static std::atomic<unsigned> allocations{0};
void* operator new(std::size_t n){if(measured)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
struct Meter:spike::Display {
 spike::Surface out,ink;std::span<const std::uint32_t> bg;bool compose{};std::uint64_t count{};std::array<std::uint64_t,1024> tails{};
 spike::Submit submit(spike::Rect r)noexcept override{auto start=spike::monotonic();try{if(compose)underlay::compose(out,bg,ink,r);}catch(...){return spike::Submit::Failed;}tails[count++]=spike::monotonic()-start;return spike::Submit::Accepted;}
};
input_event event(int type,int code,int value){input_event e{};e.type=type;e.code=code;e.value=value;return e;}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=3)return 64;using namespace underlay;
 try{for(int mode=0;mode<3;++mode){auto data=std::filesystem::path(argv[2])/std::to_string(mode);auto p=prepare(argv[1],data);auto output=p.background;Meter meter;meter.out={output,Width,Height,Stride};meter.ink={p.ink,Width,Height,Stride};meter.bg=p.background;meter.compose=mode!=0;
  auto engine=std::make_unique<spike::Engine>(spike::Transform{{0,Width-1},{0,Height-1},Width,Height,0,false},meter.ink,meter);spike::FrameAssembler assembler;spike::Scheduler schedule;spike::SessionStats stats;
  std::unique_ptr<Pipeline> pipe;if(mode)pipe=std::make_unique<Pipeline>(data/"annotations.ink",p,IoFault{-1,false,false,mode==2?100u:0u});
  std::array<std::uint64_t,1000> latency{};unsigned count=0;allocations=0;
  auto feed=[&](std::span<input_event> ev){auto begin=spike::monotonic();measured=true;bool ok=spike::consumeEvents(ev,assembler,*engine,schedule,stats,1+std::uint64_t(count)*8000000,nullptr,pipe.get());measured=false;if(!ok)throw std::runtime_error("matched-input-rejected");latency[count++]=spike::monotonic()-begin;std::this_thread::sleep_for(std::chrono::microseconds(100));};
  for(int stroke=0;stroke<4;++stroke){bool eraser=stroke==2;std::array down={event(EV_KEY,BTN_TOOL_PEN,!eraser),event(EV_KEY,BTN_TOOL_RUBBER,eraser),event(EV_ABS,ABS_X,100),event(EV_ABS,ABS_Y,200+stroke*20),event(EV_KEY,BTN_TOUCH,1),event(EV_SYN,SYN_REPORT,0)};feed(down);for(int i=1;i<100;++i){std::array move={event(EV_ABS,ABS_X,100+i*3),event(EV_SYN,SYN_REPORT,0)};feed(move);}std::array up={event(EV_KEY,BTN_TOUCH,0),event(EV_SYN,SYN_REPORT,0)};feed(up);}
  if(pipe&&!pipe->finish(2000))throw std::runtime_error("writer-timeout");if(pipe&&(pipe->committed!=4||pipe->unsaved()))throw std::runtime_error("uncommitted-work");std::sort(latency.begin(),latency.begin()+count);std::sort(meter.tails.begin(),meter.tails.begin()+meter.count);
  std::printf("MATCHED_PERF mode=%s accepted=%llu contacts=%llu releases=%llu submits=%llu live_cpp_allocations=%u processing_p99_ns=%llu processing_max_ns=%llu presentation_p99_ns=%llu queue_high_water=%u rejection_count=%u unsaved=%u optical=false\n",mode==0?"qualified_blank":mode==1?"underlay_async":"underlay_delayed_writer",(unsigned long long)stats.samples,(unsigned long long)stats.contacts,(unsigned long long)stats.released,(unsigned long long)meter.count,allocations.load(),(unsigned long long)latency[count*99/100],(unsigned long long)latency[count-1],(unsigned long long)meter.tails[meter.count*99/100],pipe?pipe->highWater.load():0,pipe?pipe->rejected.load():0,pipe?pipe->unsaved():0);
 }}catch(const std::exception& e){std::fprintf(stderr,"PERF_FAIL %s\n",e.what());return 1;}return 0;}
