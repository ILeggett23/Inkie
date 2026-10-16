#include "ux_support.h"
#include <QCoreApplication>
#include <algorithm>
#include <cstdlib>
#include <new>
thread_local bool measured=false;static std::atomic<unsigned> allocations{0};
void* operator new(std::size_t n){if(measured)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
struct Meter:spike::Display {ux::Shell& shell;std::vector<std::uint32_t> out;std::array<spike::Ns,1024> tails{};unsigned count{};explicit Meter(ux::Shell& s):shell(s),out(underlay::Pixels,0xffffffffu){}spike::Submit submit(spike::Rect r)noexcept override{auto start=spike::monotonic();try{underlay::compose({out,1620,2160,1632},shell.background(),shell.ink(),r);}catch(...){return spike::Submit::Failed;}tails[count++]=spike::monotonic()-start;return spike::Submit::Accepted;}};
input_event ev(int type,int code,int value){input_event e{};e.type=type;e.code=code;e.value=value;return e;}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=3)return 64;try{for(unsigned mode=0;mode<4;++mode){auto data=std::filesystem::path(argv[2])/std::to_string(mode);ux::Shell shell(argv[1],data);Canvas canvas(shell);Meter meter(shell);CHECK(shell.choose(0));until([&]{shell.tick();CHECK(!shell.failed());return shell.page()>=0;});
 auto engine=std::make_unique<spike::Engine>(spike::Transform{{0,1619},{0,2159},1620,2160,0,false},shell.ink(),meter);spike::FrameAssembler assembler;spike::Scheduler scheduler;spike::SessionStats stats;spike::InputGate gate;
 std::array<spike::Ns,404> latency{};unsigned count=0,preparing=0;allocations=0;canvas.changes();
 if(mode!=3)until([&]{shell.tick();canvas.changes();return shell.document()->resident()>=2&&!shell.document()->preparing();});else until([&]{return shell.document()->preparing();});
 if(mode==1||mode==2){CHECK(shell.action(ux::Action::Show));canvas.changes();}if(mode==2){CHECK(shell.action(ux::Action::Hide));canvas.changes();}
 auto start=spike::monotonic();
 auto feed=[&](auto& events){auto begin=spike::monotonic();measured=true;bool ok=spike::consumeEvents(events,assembler,*engine,scheduler,stats,1+std::uint64_t(count)*8000000,nullptr,&shell,&gate);measured=false;CHECK(ok);latency[count++]=spike::monotonic()-begin;preparing+=shell.document()->preparing();std::this_thread::sleep_for(std::chrono::microseconds(100));};
 for(int stroke=0;stroke<4;++stroke){bool erase=stroke==2;std::array down={ev(EV_KEY,BTN_TOOL_PEN,!erase),ev(EV_KEY,BTN_TOOL_RUBBER,erase),ev(EV_ABS,ABS_X,100),ev(EV_ABS,ABS_Y,200+stroke*20),ev(EV_KEY,BTN_TOUCH,1),ev(EV_SYN,SYN_REPORT,0)};feed(down);for(int i=1;i<100;++i){std::array move={ev(EV_ABS,ABS_X,100+i*3),ev(EV_SYN,SYN_REPORT,0)};feed(move);}std::array up={ev(EV_KEY,BTN_TOUCH,0),ev(EV_SYN,SYN_REPORT,0)};feed(up);}
 auto end=spike::monotonic();CHECK(stats.samples==404&&stats.contacts==4&&count==404&&meter.count==404&&!gate.rejected&&!allocations);if(mode==3)CHECK(preparing>0);CHECK(shell.finish(2000)&&!shell.failed()&&shell.committed()==4&&shell.unsaved()==0);
 std::sort(latency.begin(),latency.end());std::sort(meter.tails.begin(),meter.tails.begin()+meter.count);const char* names[]={"B_hidden","C_visible_idle","D_after_hide","E_active_prefetch"};
 std::printf("UX_PERF mode=%s accepted=404 contacts=4 submits=%u live_cpp_allocations=%u processing_p50_ns=%llu processing_p99_ns=%llu processing_max_ns=%llu composition_p99_ns=%llu composition_max_ns=%llu queue_high_water=%u preparing_samples=%u start_ns=%llu end_ns=%llu live_mupdf=0 live_storage_waits=0 live_ui_waits=0 shared_lock_waits=0 dropped_accepted=0 optical=false\n",names[mode],meter.count,allocations.load(),(unsigned long long)latency[202],(unsigned long long)latency[399],(unsigned long long)latency[403],(unsigned long long)meter.tails[399],(unsigned long long)meter.tails[403],shell.document()->active().saves->highWater.load(),preparing,(unsigned long long)start,(unsigned long long)end);
 std::printf("UX_BASELINE_P95 mode=%s processing_p95_ns=%llu composition_p95_ns=%llu\n",names[mode],(unsigned long long)latency[383],(unsigned long long)meter.tails[383]);
 }return 0;}catch(const std::exception& e){std::fprintf(stderr,"UX_PERF_FAIL %s\n",e.what());return 1;}}
