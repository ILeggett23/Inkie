#include "reader.h"
#include <QCoreApplication>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>
thread_local bool measured=false;static std::atomic<unsigned> allocations{0};
void* operator new(std::size_t n){if(measured)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
struct Meter:spike::Display {spike::Surface out,ink;std::span<const std::uint32_t> bg;std::uint64_t count{};std::array<std::uint64_t,1024> tails{};spike::Submit submit(spike::Rect r)noexcept override{auto start=spike::monotonic();try{underlay::compose(out,bg,ink,r);}catch(...){return spike::Submit::Failed;}tails[count++]=spike::monotonic()-start;return spike::Submit::Accepted;}};
input_event event(int type,int code,int value){input_event e{};e.type=type;e.code=code;e.value=value;return e;}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=3)return 64;using namespace underlay;try{
 for(int mode=0;mode<3;++mode){auto data=std::filesystem::path(argv[2])/std::to_string(mode);auto p=reader::initialPrepare(argv[1],data);reader::Reader reader(argv[1],data,std::move(p));reader.activate();
 auto deadline=spike::monotonic()+15000000000ULL;
 if(mode!=1){while(reader.resident()<2||reader.preparing()){reader.tick();if(spike::monotonic()>deadline)throw std::runtime_error("idle-prefetch-timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
 else{while(!reader.preparing()){if(spike::monotonic()>deadline)throw std::runtime_error("active-prefetch-timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
 auto& page=reader.active().prepared;auto output=page.background;Meter meter;meter.out={output,Width,Height,Stride};meter.ink={page.ink,Width,Height,Stride};meter.bg=page.background;
 auto engine=std::make_unique<spike::Engine>(spike::Transform{{0,Width-1},{0,Height-1},Width,Height,0,false},meter.ink,meter);spike::FrameAssembler assembler;spike::Scheduler schedule;spike::SessionStats stats;spike::InputGate gate;
 std::array<std::uint64_t,1000> latency{};unsigned count=0,preparingSamples=0,queuedSamples=0,commitChanges=0;unsigned lastCommitted=0;allocations=0;auto wallStart=spike::monotonic();
 auto feed=[&](std::span<input_event> ev){auto begin=spike::monotonic();measured=true;bool ok=spike::consumeEvents(ev,assembler,*engine,schedule,stats,1+std::uint64_t(count)*8000000,nullptr,&reader,&gate);measured=false;if(!ok)throw std::runtime_error("matched-input-rejected");latency[count++]=spike::monotonic()-begin;
  preparingSamples+=reader.preparing();queuedSamples+=reader.active().saves->queued.load()>0;auto committed=reader.active().saves->committed.load();commitChanges+=committed!=lastCommitted;lastCommitted=committed;
  // Pacing is outside measured calls, same 404-event trace. It allows actual
  // background work to overlap instead of a sub-millisecond synthetic burst.
  std::this_thread::sleep_for(std::chrono::microseconds(100));};
 for(int stroke=0;stroke<4;++stroke){bool eraser=stroke==2;std::array down={event(EV_KEY,BTN_TOOL_PEN,!eraser),event(EV_KEY,BTN_TOOL_RUBBER,eraser),event(EV_ABS,ABS_X,100),event(EV_ABS,ABS_Y,200+stroke*20),event(EV_KEY,BTN_TOUCH,1),event(EV_SYN,SYN_REPORT,0)};feed(down);for(int i=1;i<100;++i){std::array move={event(EV_ABS,ABS_X,100+i*3),event(EV_SYN,SYN_REPORT,0)};feed(move);}std::array up={event(EV_KEY,BTN_TOUCH,0),event(EV_SYN,SYN_REPORT,0)};feed(up);}
 auto wallEnd=spike::monotonic();if(!reader.finish(2000)||reader.failed()||reader.unsaved()||reader.committed()!=4||stats.samples!=404||stats.contacts!=4||allocations||gate.rejected)throw std::runtime_error("matched-gate-failed");if(mode==1&&!preparingSamples)throw std::runtime_error("prefetch-not-overlapped");if(mode==2&&(!queuedSamples||!commitChanges))throw std::runtime_error("storage-not-overlapped");
 std::sort(latency.begin(),latency.begin()+count);std::sort(meter.tails.begin(),meter.tails.begin()+meter.count);
 std::printf("READER_PERF mode=%s accepted=%llu contacts=%llu submits=%llu live_cpp_allocations=%u processing_p50_ns=%llu processing_p99_ns=%llu processing_max_ns=%llu composition_p99_ns=%llu queue_high_water=%u preparing_samples=%u queued_samples=%u commit_changes_in_trace=%u start_ns=%llu end_ns=%llu live_mupdf=0 live_storage_waits=0 optical=false\n",mode==0?"B_idle_prefetch":mode==1?"C_active_helper":"D_actual_storage",(unsigned long long)stats.samples,(unsigned long long)stats.contacts,(unsigned long long)meter.count,allocations.load(),(unsigned long long)latency[count/2],(unsigned long long)latency[count*99/100],(unsigned long long)latency[count-1],(unsigned long long)meter.tails[meter.count*99/100],reader.active().saves->highWater.load(),preparingSamples,queuedSamples,commitChanges,(unsigned long long)wallStart,(unsigned long long)wallEnd);
 }return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"PERF_FAIL %s\n",e.what());return 1;}}
