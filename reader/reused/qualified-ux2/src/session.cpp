// SPDX-License-Identifier: AGPL-3.0-only
#include "session.h"
#include "session_limits.h"
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <algorithm>
namespace spike {
static volatile sig_atomic_t interrupted=0;
static void stopSignal(int) {interrupted=1;}
int Scheduler::waitMs(Ns now,bool dirty)const noexcept {
 if(due(now,dirty))return 0;
 if(!dirty)return 2;
 if(now<last)return 0;
 Ns left=Interval-(now-last);return int(std::min<Ns>(2,(left+999999)/1000000));
}
bool consumeEvents(std::span<const input_event> events,FrameAssembler& a,Engine&e,Scheduler&s,SessionStats& stats,Ns now,RecoveryLease* lease,OperationSink* sink,InputGate* gate)noexcept{
 for(const auto& raw:events){
  if(lease&&(!lease->valid()||lease->stopRequested()||monotonic()>=lease->deadline())){stats.failed=stats.cancelled=true;e.inputFailed();return false;}
  Sample sample;auto result=a.feed(raw.type,raw.code,raw.value,now,sample);
  if(result==FrameResult::Fault){stats.failed=stats.cancelled=true;e.inputFailed();return false;}
  if(result!=FrameResult::Ready)continue;
  if(gate){Ns eventTime=raw.time.tv_sec>=0&&raw.time.tv_usec>=0&&raw.time.tv_usec<1000000?Ns(raw.time.tv_sec)*1000000000ULL+Ns(raw.time.tv_usec)*1000:0;
   if(gate->blocked||!gate->open||(sample.phase==Phase::Down&&eventTime<gate->notBefore)){gate->blocked=sample.phase!=Phase::Up;++gate->rejected;continue;}}
  if(stats.samples>=16384){stats.failed=stats.cancelled=true;e.inputFailed();return false;}
  if(sample.phase==Phase::Down&&stats.contacts>=5){stats.failed=stats.cancelled=true;e.inputFailed();return false;}
  if(sink&&sample.phase==Phase::Down&&!sink->reserve()){std::fputs("UNDERLAY_ADMISSION_REJECTED persistence_unavailable_or_capacity\n",stderr);stats.failed=stats.cancelled=true;e.inputFailed();return false;}
  if(!e.accept(sample)){if(sink)sink->cancel();stats.failed=true;return false;}
  if(sink&&!sink->append(e.points().back())){stats.failed=true;return false;}
  ++stats.samples;if(sample.phase==Phase::Down){++stats.contacts;if(sample.eraser)++stats.eraserContacts;else ++stats.penContacts;}if(sample.phase==Phase::Up)++stats.released;
  s.observe(sample.phase);
  // Contact edges cannot be hidden by subsequent frames in this input batch.
  if(s.due(now,!e.dirty().empty())){if(e.flush()!=Submit::Accepted){stats.failed=true;return false;}++stats.submits;s.submitted(now);}
 }
 return true;
}
int runDirectSession(RecoveryLease& lease,SessionDisplay& display)noexcept{
 auto rejected=[](const char* stage){std::fprintf(stderr,"DIRECT_STARTUP_REJECT stage=%s errno=%d\n",stage,errno);std::fflush(stderr);return 78;};
 Ns start=monotonic();
 if(!lease.valid()||!recovery::limits::validDeadline(start,lease.deadline()))return rejected("lease");
 interrupted=0;struct sigaction sa{};sa.sa_handler=stopSignal;sigemptyset(&sa.sa_mask);
 if(sigaction(SIGTERM,&sa,nullptr)||sigaction(SIGINT,&sa,nullptr))return rejected("signal_handlers");
 // Discovery/grab occurs only with exclusive ownership and valid external recovery.
 Marker marker;if(!marker.openExclusive(lease.valid()))return rejected("marker_open");
 std::fprintf(stderr,"DIRECT_STARTUP marker_open=ok\n");
 if(!lease.valid()||!display.initialize(lease)){display.shutdown();return rejected("vendor_initialize");}
 Surface surface=display.surface();if(!surface.valid()||!display.initialPage()){display.shutdown();return rejected("surface_initial_page");}
 Transform transform{marker.metadata.x,marker.metadata.y,surface.width,surface.height,0,false};
 auto engine=std::unique_ptr<Engine>(new(std::nothrow)Engine(transform,surface,display));if(!engine){display.shutdown();return rejected("engine_allocation");}
 // One small synthetic segment is part of the future visible initialization gate.
 // It is distinct from contact counts and uses the same regional submit policy.
 if(display.diagnosticSegment()){
  Rect smoke=segment(surface,{40,40,Phase::Down,start},{100,40,Phase::Move,start},2);
  if(smoke.empty()||display.submit(smoke)!=Submit::Accepted){display.shutdown();return rejected("static_segment");}
 }
 std::fprintf(stderr,"DIRECT_INK_READY width=%d height=%d stride_pixels=%d\n",surface.width,surface.height,surface.stridePixels);std::fflush(stderr);
 FrameAssembler assembly(marker.metadata.p,marker.metadata.pressure);Scheduler schedule;SessionStats stats;
 InputGate gate;gate.notBefore=display.pageVersion()?monotonic():0;std::uint64_t pageVersion=display.pageVersion();
 struct Aggregate{Ns minimum{~Ns(0)},maximum{},total{};std::size_t count{};};std::array<Aggregate,3> aggregate{};
 auto collect=[&]{for(int metric=0;metric<3;++metric)for(const auto&t:engine->timing()){Ns b=metric==0?t.t0:metric==1?t.t1:t.t3,f=metric==0?t.t1:metric==1?t.t2:t.t4;if(!b||f<b)continue;auto& a=aggregate[metric];Ns elapsed=f-b;a.minimum=std::min(a.minimum,elapsed);a.maximum=std::max(a.maximum,elapsed);a.total+=elapsed;++a.count;}};
 const Ns end=lease.deadline(); // One immutable owner deadline; never renewed at readiness.
 std::array<input_event,64> buffer{};
 while(!interrupted&&!lease.stopRequested()){
  Ns now=monotonic();if(!now||now>=end||!lease.valid())break;
  pollfd fd{marker.fd,POLLIN,0};int ready=poll(&fd,1,schedule.waitMs(now,!engine->dirty().empty()));
  if(ready<0){if(errno==EINTR)continue;stats.failed=true;break;}
  if(fd.revents&(POLLERR|POLLHUP|POLLNVAL)){stats.failed=true;break;}
  if(fd.revents&POLLIN){
   ssize_t n=read(marker.fd,buffer.data(),sizeof(buffer));
   if(n<0){if(errno!=EAGAIN&&errno!=EINTR){stats.failed=true;break;}}
   else if(!validEventBytes(std::size_t(n))){stats.failed=stats.cancelled=true;engine->inputFailed();break;}
   else {gate.open=display.admissionOpen();if(!consumeEvents({buffer.data(),std::size_t(n)/sizeof(input_event)},assembly,*engine,schedule,stats,monotonic(),&lease,display.operationSink(),pageVersion?&gate:nullptr))break;}
  }
  // Fifth release does not dismiss the page. User requests owner stop; a sixth
  // contact still fails closed in consumeEvents, preserving the contact budget.
  now=monotonic();if(!lease.valid()||!now||now>=end)break;
  if(schedule.due(now,!engine->dirty().empty())){if(engine->flush()!=Submit::Accepted){stats.failed=true;break;}++stats.submits;schedule.submitted(now);}
  display.pump();
  pollfd pending{marker.fd,POLLIN,0};
  if(!assembly.contactActive()&&poll(&pending,1,0)==0){if(!display.readerBoundary())break;if(display.pageVersion()!=pageVersion){collect();surface=display.surface();auto next=std::unique_ptr<Engine>(new(std::nothrow)Engine(transform,surface,display));if(!next){stats.failed=true;break;}engine=std::move(next);pageVersion=display.pageVersion();gate.notBefore=monotonic();display.pageInstalled();}}
 }
 // Stop admission first. No last submit after lease loss/deadline; pending damage
 // is explicitly incomplete rather than invented as displayed.
 marker.close();if(display.operationSink())display.operationSink()->cancel();stats.pressure=assembly.evidence();bool pending=!engine->dirty().empty();display.shutdown();
 std::printf("DIRECT_SUMMARY samples=%llu contacts=%llu releases=%llu pen_contacts=%llu eraser_contacts=%llu submits=%llu pressure_frames=%llu pressure_min=%d pressure_max=%d rubber_frames=%llu dropped=%llu failed=%d cancelled=%d pending=%d optical_latency=NOT_MEASURED\n",(unsigned long long)stats.samples,(unsigned long long)stats.contacts,(unsigned long long)stats.released,(unsigned long long)stats.penContacts,(unsigned long long)stats.eraserContacts,(unsigned long long)stats.submits,(unsigned long long)stats.pressure.pressureFrames,stats.pressure.min,stats.pressure.max,(unsigned long long)stats.pressure.rubberFrames,(unsigned long long)stats.pressure.drops,stats.failed,stats.cancelled,pending);
 collect();std::fprintf(stderr,"READER_INPUT_GATE unadmitted_samples=%llu\n",(unsigned long long)gate.rejected);
 for(int metric=0;metric<3;++metric){
  Ns minimum=~Ns(0),maximum=0,total=0;std::size_t count=0;
  minimum=aggregate[metric].minimum;maximum=aggregate[metric].maximum;total=aggregate[metric].total;count=aggregate[metric].count;
  std::printf("DIRECT_TIMING metric=%s count=%zu min_ns=%llu max_ns=%llu sum_ns=%llu boundary=application_local_shared_batch_or_submit optical_completion=false\n",metric==0?"receipt_to_accept":metric==1?"raster":"submit_call_wall",count,(unsigned long long)(count?minimum:0),(unsigned long long)maximum,(unsigned long long)total);
 }
 return stats.failed?1:0;
}
}
