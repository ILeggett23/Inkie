// SPDX-License-Identifier: AGPL-3.0-only
#include "vendor.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <functional>
#include <new>
#include <atomic>
#include <limits>
using namespace spike;
static std::atomic<std::size_t> allocations{0};
void*operator new(std::size_t n){++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);std::abort();}}while(0)
struct Lease:RecoveryLease{bool alive=true,stop=false;Ns until=monotonic()+10000000000ULL;bool valid()const noexcept override{return alive;}Ns deadline()const noexcept override{return until;}bool stopRequested()const noexcept override{return stop;}};
struct Calls:VendorCalls{
 std::vector<std::uint32_t> pixels=std::vector<std::uint32_t>(1632*2160,0x12345678);Pixels descriptor{reinterpret_cast<unsigned char*>(pixels.data()),1620,2160,4,6528};
 unsigned acquired{},called{},pumped{};int content=-1,mode=-1,flags=-1;Rect last{};bool acquireOK=true;std::function<void()> nested;
 bool acquire(Pixels&p)noexcept override{++acquired;p=descriptor;return acquireOK;}
 void region(Rect r,int c,int m,int f)noexcept override{++called;last=r;content=c;mode=m;flags=f;if(nested)nested();}
 void events()noexcept override{++pumped;}
};
struct Fixture{std::array<std::uint32_t,128*96> p{};FakeDisplay d;Engine e;Scheduler s;SessionStats stats;FrameAssembler a;
 Fixture():e({{0,127},{0,95},128,96,0,false},{p,128,96,128},d){p.fill(0xffffffff);}
};
static input_event event(unsigned type,unsigned code,int value){input_event e{};e.type=type;e.code=code;e.value=value;return e;}
static FrameResult feed(FrameAssembler&a,unsigned t,unsigned c,int v,Sample&s){return a.feed(t,c,v,100,s);}
static FrameResult down(FrameAssembler&a,Sample&s){feed(a,EV_ABS,ABS_X,10,s);feed(a,EV_ABS,ABS_Y,20,s);feed(a,EV_KEY,BTN_TOOL_PEN,1,s);feed(a,EV_KEY,BTN_TOUCH,1,s);return feed(a,EV_SYN,SYN_REPORT,0,s);}
static MarkerMetadata metadata(){MarkerMetadata m;std::strcpy(m.name.data(),"Elan marker input");m.bus=BUS_SPI;m.pen=m.rubber=m.touch=m.absX=m.absY=m.pressure=true;m.x={10,11180};m.y={20,15340};m.p={0,4095};return m;}
static void test(const char*n,const std::function<void()>&f){f();std::printf("PASS %s\n",n);}
int main(){unsigned count=0;auto run=[&](const char*n,const std::function<void()>&f){test(n,f);++count;};
 run("marker_press",[]{FrameAssembler a;Sample s;CHECK(down(a,s)==FrameResult::Ready&&s.phase==Phase::Down&&s.x==10&&s.y==20);});
 run("marker_move",[]{FrameAssembler a;Sample s;down(a,s);feed(a,EV_ABS,ABS_X,11,s);CHECK(feed(a,EV_SYN,SYN_REPORT,0,s)==FrameResult::Ready&&s.phase==Phase::Move&&s.x==11);});
 run("marker_release",[]{FrameAssembler a;Sample s;down(a,s);feed(a,EV_KEY,BTN_TOUCH,0,s);CHECK(feed(a,EV_SYN,SYN_REPORT,0,s)==FrameResult::Ready&&s.phase==Phase::Up);});
 run("pressure_aggregate_and_stationary_frame",[]{FrameAssembler a;Sample s;down(a,s);feed(a,EV_ABS,ABS_PRESSURE,600,s);CHECK(feed(a,EV_SYN,SYN_REPORT,0,s)==FrameResult::Ready&&s.pressure==600&&s.pressurePresent);feed(a,EV_ABS,ABS_PRESSURE,1200,s);feed(a,EV_SYN,SYN_REPORT,0,s);CHECK(a.evidence().min==600&&a.evidence().max==1200&&a.evidence().pressureFrames==2);});
 run("invalid_pressure",[]{FrameAssembler a;Sample s;CHECK(feed(a,EV_ABS,ABS_PRESSURE,65536,s)==FrameResult::Fault&&a.cancelled());});
 run("pressure_absent_allowed",[]{FrameAssembler a({},false);Sample s;CHECK(down(a,s)==FrameResult::Ready&&!s.pressurePresent);});
 run("eraser_down_coherent_sample",[]{FrameAssembler a;Sample s;feed(a,EV_ABS,ABS_X,10,s);feed(a,EV_ABS,ABS_Y,20,s);feed(a,EV_KEY,BTN_TOOL_RUBBER,1,s);feed(a,EV_KEY,BTN_TOUCH,1,s);CHECK(feed(a,EV_SYN,SYN_REPORT,0,s)==FrameResult::Ready&&s.eraser&&s.phase==Phase::Down&&a.evidence().rubberFrames==1);});
 run("tool_transition_cancels",[]{FrameAssembler a;Sample s;down(a,s);feed(a,EV_KEY,BTN_TOOL_RUBBER,1,s);CHECK(feed(a,EV_SYN,SYN_REPORT,0,s)==FrameResult::Fault);});
 run("syn_dropped_fail_stop_clears_contact",[]{FrameAssembler a;Sample s;down(a,s);feed(a,EV_ABS,ABS_PRESSURE,1000,s);CHECK(feed(a,EV_SYN,SYN_DROPPED,0,s)==FrameResult::Fault&&a.cancelled());CHECK(down(a,s)==FrameResult::Fault&&a.evidence().drops==1);});
 run("not_every_syn_is_report",[]{FrameAssembler a;Sample s;feed(a,EV_KEY,BTN_TOOL_PEN,1,s);feed(a,EV_KEY,BTN_TOUCH,1,s);CHECK(feed(a,EV_SYN,SYN_CONFIG,0,s)==FrameResult::None);});
 run("missing_coordinates_rejected",[]{FrameAssembler a;Sample s;feed(a,EV_KEY,BTN_TOOL_PEN,1,s);feed(a,EV_KEY,BTN_TOUCH,1,s);CHECK(feed(a,EV_SYN,SYN_REPORT,0,s)==FrameResult::Fault);});
 run("invalid_key",[]{FrameAssembler a;Sample s;CHECK(feed(a,EV_KEY,BTN_TOUCH,2,s)==FrameResult::Fault);});
 run("hover_no_samples",[]{FrameAssembler a;Sample s;feed(a,EV_KEY,BTN_TOOL_PEN,1,s);feed(a,EV_ABS,ABS_X,10,s);feed(a,EV_ABS,ABS_Y,20,s);CHECK(feed(a,EV_SYN,SYN_REPORT,0,s)==FrameResult::None);});
 run("truncated_read_rejected",[]{CHECK(!validEventBytes(1)&&!validEventBytes(sizeof(input_event)+1)&&!validEventBytes(0)&&!validEventBytes(65*sizeof(input_event))&&validEventBytes(64*sizeof(input_event)));});
 run("device_metadata_valid",[]{CHECK(metadata().valid());});
 run("device_name_and_bus_rejected",[]{auto m=metadata();m.name[0]='x';CHECK(!m.valid());m=metadata();m.bus=BUS_USB;CHECK(!m.valid());});
 run("device_caps_rejected",[]{auto m=metadata();m.pen=false;CHECK(!m.valid());m=metadata();m.absY=false;CHECK(!m.valid());});
 run("axis_invalid_no_fallback",[]{auto m=metadata();m.x.max=m.x.min;CHECK(!m.valid());m=metadata();m.p.max=-1;CHECK(!m.valid());});
 run("axis_nonzero_minima",[]{Transform t{{10,110},{20,220},128,96,0,false};Point p;CHECK(t.map({10,20,Phase::Down,1},p)&&p.x==0&&p.y==0);CHECK(t.map({110,220,Phase::Move,2},p)&&p.x==127&&p.y==95);});
 run("exclusive_input_gate_no_open",[]{Marker m;CHECK(!m.openExclusive(false)&&m.fd==-1);});
 run("initial_dot",[]{Fixture f;CHECK(f.e.accept({10,10,Phase::Down,1}));CHECK(f.p[10*128+10]==0xff000000&&f.e.dirty().contains(10,10));});
 run("straight_line_endpoint",[]{Fixture f;f.e.accept({10,10,Phase::Down,1});CHECK(f.e.accept({30,10,Phase::Up,2}));for(int x=10;x<=30;++x)CHECK(f.p[10*128+x]==0xff000000);});
 run("diagonal_and_corner",[]{Fixture f;f.e.accept({10,10,Phase::Down,1});f.e.accept({30,30,Phase::Move,2});f.e.accept({40,20,Phase::Up,3});CHECK(f.p[30*128+30]==0xff000000&&f.p[20*128+40]==0xff000000);});
 run("clipping_and_dirty_bounds",[]{Fixture f;f.e.accept({0,0,Phase::Down,1});CHECK(f.e.dirty().x0==0&&f.e.dirty().y0==0&&f.e.dirty().x1==3&&f.e.dirty().y1==3);});
 run("no_hover_drawing_pipeline",[]{Fixture f;auto a=event(EV_ABS,ABS_X,20);auto b=event(EV_SYN,SYN_REPORT,0);input_event events[]={a,b};CHECK(consumeEvents(events,f.a,f.e,f.s,f.stats,1)&&f.e.points().empty()&&f.d.calls==0);});
 run("stroke_overflow_fail_stop",[]{Fixture f;CHECK(f.e.accept({10,10,Phase::Down,1}));for(int i=1;i<16384;++i)CHECK(f.e.accept({10,10,Phase::Move,Ns(i+1)}));CHECK(!f.e.accept({10,10,Phase::Move,20000}));});
 run("sample_clock_reset_rejected",[]{Fixture f;f.e.accept({1,1,Phase::Down,100});CHECK(!f.e.accept({2,2,Phase::Move,99}));});
 run("immediate_first_contact",[]{Scheduler s;s.observe(Phase::Down);CHECK(s.due(1,true));s.submitted(1);CHECK(!s.due(2,true));});
 run("cadence_exact_8ms",[]{Scheduler s;s.submitted(100);CHECK(!s.due(8000099,true)&&s.due(8000100,true));});
 run("pen_up_flush",[]{Scheduler s;s.submitted(100);s.observe(Phase::Up);CHECK(s.due(101,true));});
 run("no_empty_submit",[]{Fixture f;CHECK(f.e.flush()==Submit::Accepted&&f.d.calls==0);Scheduler s;s.observe(Phase::Down);CHECK(!s.due(1,false));});
 run("dirty_region_merge",[]{Fixture f;f.e.accept({10,10,Phase::Down,1});f.e.flush();f.e.accept({20,20,Phase::Move,2});f.e.accept({30,10,Phase::Move,3});CHECK(f.e.dirty().contains(10,10)&&f.e.dirty().contains(30,10));});
 run("batch_preserves_contact_edges",[]{Fixture f;input_event ev[]={event(EV_ABS,ABS_X,10),event(EV_ABS,ABS_Y,20),event(EV_KEY,BTN_TOOL_PEN,1),event(EV_KEY,BTN_TOUCH,1),event(EV_SYN,SYN_REPORT,0),event(EV_ABS,ABS_X,20),event(EV_SYN,SYN_REPORT,0),event(EV_KEY,BTN_TOUCH,0),event(EV_SYN,SYN_REPORT,0)};CHECK(consumeEvents(ev,f.a,f.e,f.s,f.stats,100));CHECK(f.stats.samples==3&&f.stats.contacts==1&&f.stats.released==1&&f.d.calls==2&&f.e.submitted()==3);});
 run("pipeline_syn_drop_cancels_without_flush",[]{Fixture f;input_event ev[]={event(EV_SYN,SYN_DROPPED,0)};CHECK(!consumeEvents(ev,f.a,f.e,f.s,f.stats,1)&&f.stats.cancelled&&f.d.calls==0);});
 run("capture_deduplicates_aliases",[]{Calls c;CaptureTable t;t.observe(c.descriptor);t.observe(c.descriptor);Pixels p;CHECK(t.select(p)&&p.data==c.descriptor.data);});
 run("capture_ignores_mask",[]{Calls c;CaptureTable t;t.observe({nullptr,0,0,24,0});t.observe(c.descriptor);Pixels p;CHECK(t.select(p));});
 run("capture_ambiguity_rejected",[]{Calls c;CaptureTable t;t.observe(c.descriptor);auto p=c.descriptor;p.data+=4;t.observe(p);CHECK(!t.select(p));});
 run("capture_bad_format_geometry_stride_alignment",[]{Calls c;for(int i=0;i<4;++i){CaptureTable t;auto p=c.descriptor;if(i==0)p.format=7;if(i==1)p.width=0;if(i==2)p.stride=6480;if(i==3)++p.data;t.observe(p);CHECK(!t.select(p));}});
 run("vendor_requires_lease_before_acquire",[]{Calls c;Lease l;l.alive=false;VendorDisplay d(c);CHECK(!d.initialize(l)&&c.acquired==0);});
 run("vendor_init_failure_no_draw",[]{Calls c;c.acquireOK=false;Lease l;VendorDisplay d(c);CHECK(!d.initialize(l)&&!d.surface().valid()&&c.called==0);});
 run("vendor_initial_and_live_policy",[]{Calls c;Lease l;VendorDisplay d(c);CHECK(d.initialize(l)&&d.initialPage());CHECK(c.mode==3&&c.content==0&&c.flags==1&&c.pixels[0]==0xffffffff&&c.pixels[1620]==0x12345678);CHECK(d.submit({10,10,20,20})==Submit::Accepted&&c.mode==0&&c.content==0&&c.flags==0&&c.last.x1==20);CHECK(d.ui({0,0,1620,1200})==Submit::Accepted&&c.mode==3&&c.content==0&&c.flags==0);CHECK(d.clean({0,0,400,400})==Submit::Accepted&&c.mode==3&&c.content==0&&c.flags==0);});
 run("vendor_return_is_only_local_normal_return",[]{Calls c;Lease l;VendorDisplay d(c);CHECK(d.initialize(l));CHECK(d.submit({1,1,2,2})==Submit::Accepted&&c.called==1);});
 run("vendor_reentrancy_rejected",[]{Calls c;Lease l;VendorDisplay d(c);CHECK(d.initialize(l));c.nested=[&]{CHECK(d.submit({0,0,1,1})==Submit::Failed);};CHECK(d.submit({0,0,1,1})==Submit::Accepted&&c.called==1);});
 run("vendor_call_budget",[]{Calls c;Lease l;VendorDisplay d(c);CHECK(d.initialize(l));for(unsigned i=0;i<VendorDisplay::MaxSubmissions;++i)CHECK(d.submit({0,0,1,1})==Submit::Accepted);CHECK(d.submit({0,0,1,1})==Submit::Failed&&c.called==512);});
 run("vendor_sustained_no_lifetime_budget",[]{struct Sustained:Lease{SustainedPolicy sustained()const noexcept override{return {1000000000ull,1000,1000000};}};Calls c;Sustained l;VendorDisplay d(c);CHECK(d.initialize(l));for(unsigned i=0;i<4*VendorDisplay::MaxSubmissions;++i)CHECK(d.submit({0,0,1,1})==Submit::Accepted);for(unsigned i=0;i<64;++i)CHECK(d.submit({0,0,810,1080})==Submit::Accepted);CHECK(c.called==4*512+64&&d.submit({0,0,1620,2160})==Submit::Failed);});
 run("vendor_large_live_region_refused",[]{Calls c;Lease l;VendorDisplay d(c);CHECK(d.initialize(l));CHECK(d.submit({0,0,1620,2160})==Submit::Failed&&c.called==0);});
 run("vendor_area_budget",[]{Calls c;Lease l;VendorDisplay d(c);CHECK(d.initialize(l));for(int i=0;i<32;++i)CHECK(d.submit({0,0,810,1080})==Submit::Accepted);CHECK(d.submit({0,0,1,1})==Submit::Failed);});
 run("vendor_shutdown_retires_borrow",[]{Calls c;Lease l;VendorDisplay d(c);CHECK(d.initialize(l));d.shutdown();CHECK(!d.surface().valid()&&d.submit({0,0,1,1})==Submit::Failed&&!d.initialize(l)&&c.acquired==1);});
 run("vendor_lease_loss_and_deadline",[]{Calls c;Lease l;VendorDisplay d(c);CHECK(d.initialize(l));l.until=1;CHECK(d.submit({0,0,1,1})==Submit::Failed&&c.called==0);l.alive=false;CHECK(!d.surface().valid());});
 run("five_contact_cap_sixth_refused",[]{Fixture f;f.stats.contacts=5;input_event ev[]={event(EV_ABS,ABS_X,10),event(EV_ABS,ABS_Y,20),event(EV_KEY,BTN_TOOL_PEN,1),event(EV_KEY,BTN_TOUCH,1),event(EV_SYN,SYN_REPORT,0)};CHECK(!consumeEvents(ev,f.a,f.e,f.s,f.stats,100)&&f.e.points().empty()&&f.d.calls==0);});
 run("lease_loss_stops_input_before_mutation",[]{Fixture f;Lease l;l.alive=false;input_event ev[]={event(EV_ABS,ABS_X,10)};CHECK(!consumeEvents(ev,f.a,f.e,f.s,f.stats,100,&l)&&f.stats.cancelled&&f.e.points().empty());});
 run("eraser_whitens_only_its_brush",[]{Fixture f;f.e.accept({10,20,Phase::Down,1});f.e.accept({60,20,Phase::Up,2});CHECK(f.p[20*128+30]==0xff000000);f.e.accept({30,20,Phase::Down,3,0,false,true});f.e.accept({35,20,Phase::Up,4,0,false,true});CHECK(f.p[20*128+30]==0xffffffff&&f.p[20*128+35]==0xffffffff&&f.p[20*128+10]==0xff000000&&f.p[20*128+60]==0xff000000);CHECK(f.e.points().back().eraser);});
 run("eraser_segment_and_final_endpoint",[]{Fixture f;f.p.fill(0xff000000);CHECK(f.e.accept({5,5,Phase::Down,1,0,false,true}));CHECK(f.e.accept({20,20,Phase::Up,2,0,false,true}));for(int x=5;x<=20;++x)CHECK(f.p[x*128+x]==0xffffffff);CHECK(f.e.dirty().contains(20,20));});
 run("eraser_hover_does_not_draw",[]{Fixture f;input_event ev[]={event(EV_ABS,ABS_X,10),event(EV_ABS,ABS_Y,20),event(EV_KEY,BTN_TOOL_RUBBER,1),event(EV_SYN,SYN_REPORT,0)};CHECK(consumeEvents(ev,f.a,f.e,f.s,f.stats,100)&&f.e.points().empty()&&f.d.calls==0);});
 run("eraser_release_preserves_tool_identity",[]{FrameAssembler a;Sample s;feed(a,EV_ABS,ABS_X,1,s);feed(a,EV_ABS,ABS_Y,1,s);feed(a,EV_KEY,BTN_TOOL_RUBBER,1,s);feed(a,EV_KEY,BTN_TOUCH,1,s);feed(a,EV_SYN,SYN_REPORT,0,s);feed(a,EV_KEY,BTN_TOOL_RUBBER,0,s);feed(a,EV_KEY,BTN_TOUCH,0,s);CHECK(feed(a,EV_SYN,SYN_REPORT,0,s)==FrameResult::Ready&&s.eraser&&s.phase==Phase::Up);});
 run("pen_after_eraser_creates_new_contact",[]{FrameAssembler a;Sample s;feed(a,EV_ABS,ABS_X,1,s);feed(a,EV_ABS,ABS_Y,1,s);feed(a,EV_KEY,BTN_TOOL_RUBBER,1,s);feed(a,EV_KEY,BTN_TOUCH,1,s);feed(a,EV_SYN,SYN_REPORT,0,s);feed(a,EV_KEY,BTN_TOUCH,0,s);feed(a,EV_KEY,BTN_TOOL_RUBBER,0,s);feed(a,EV_SYN,SYN_REPORT,0,s);CHECK(down(a,s)==FrameResult::Ready&&!s.eraser&&s.phase==Phase::Down);});
 run("eraser_syn_drop_clears_without_continuation",[]{FrameAssembler a;Sample s;feed(a,EV_ABS,ABS_X,1,s);feed(a,EV_ABS,ABS_Y,1,s);feed(a,EV_KEY,BTN_TOOL_RUBBER,1,s);feed(a,EV_KEY,BTN_TOUCH,1,s);feed(a,EV_SYN,SYN_REPORT,0,s);CHECK(feed(a,EV_SYN,SYN_DROPPED,0,s)==FrameResult::Fault&&a.cancelled());});
 run("eraser_no_hot_path_cpp_allocations",[]{Fixture f;CHECK(f.e.accept({10,10,Phase::Down,1,0,false,true}));auto before=allocations.load();for(int i=0;i<1000;++i)CHECK(f.e.accept({10,10,Phase::Move,Ns(i+2),0,false,true})&&f.e.flush()==Submit::Accepted);CHECK(allocations.load()==before);});
 run("real_recovery_handoff_fails_closed",[]{CHECK(!receiveQualifiedRecoveryLease());});
 run("session_no_lease_no_input_or_vendor",[]{Calls c;Lease l;l.alive=false;VendorDisplay d(c);CHECK(runDirectSession(l,d)==78&&c.acquired==0);});
 run("no_hot_path_cpp_allocations",[]{Fixture f;Sample p;down(f.a,p);f.e.accept(p);f.e.flush();auto before=allocations.load();for(int i=0;i<1000;++i){feed(f.a,EV_ABS,ABS_PRESSURE,i,p);CHECK(feed(f.a,EV_SYN,SYN_REPORT,0,p)==FrameResult::Ready);CHECK(f.e.accept(p)&&f.e.flush()==Submit::Accepted);}CHECK(allocations.load()==before);});
 std::printf("DIRECT_HOST_TESTS_PASS cases=%u per_sample_cpp_allocations=0 vendor_calls=MOCK_ONLY raw_device_opens=0\n",count);
}
