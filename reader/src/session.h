// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "marker.h"
#include <memory>
namespace spike {
// D1 (approved 2026-09-30): V2-only sustained admission. Fixed per-window quotas
// replace the historical per-session contact/sample caps, and the deadline is
// re-read every iteration (the V2 lease reports its current grant expiry).
// A lease that reports no policy keeps the V1 limits byte-for-byte.
struct SustainedPolicy {
 Ns window{};std::uint32_t contacts{},samples{};
 bool enabled()const noexcept{return window&&contacts&&samples&&samples>=contacts;}
};
struct RecoveryLease {
 virtual bool valid()const noexcept=0;
 virtual Ns deadline()const noexcept=0;
 virtual bool stopRequested()const noexcept=0;
 virtual SustainedPolicy sustained()const noexcept{return {};}
 virtual ~RecoveryLease()=default;
};
// A one-use permit comes only from the independent PID1 recovery owner.
std::unique_ptr<RecoveryLease> receiveQualifiedRecoveryLease()noexcept;
int armOwnedSession()noexcept;
struct SessionDisplay:Display {
 virtual bool readerBoundary()noexcept{return true;}
 virtual bool admissionOpen()const noexcept{return true;}
 virtual void wakeInput(const Sample&)noexcept{}
 virtual std::uint64_t pageVersion()const noexcept{return 0;}
 virtual void pageInstalled()noexcept{}
 virtual bool diagnosticSegment()const noexcept{return true;}
 virtual struct OperationSink* operationSink()noexcept{return nullptr;}
 // The frame pen input is mapped into (docs/LANDSCAPE.md, PR 3; approved change): the display may set the Transform's
 // turn and offset, e.g. into a turned sheet's own pixels. Applied whenever the session builds an Engine (engineTransform);
 // the default keeps the surface's pixels.
 virtual void inputFrame(Transform&)noexcept{}
 virtual bool initialize(const RecoveryLease&)noexcept=0;
 virtual Surface surface()const noexcept=0;
 virtual bool initialPage()noexcept=0;
 virtual void pump()noexcept=0;
 virtual void shutdown()noexcept=0; // retire borrow, NOT proof of driver termination
};
struct Scheduler {
 static constexpr Ns Interval=8000000;
 Ns last{};bool force{};
 void observe(Phase p)noexcept{if(p==Phase::Down||p==Phase::Up)force=true;}
 bool due(Ns now,bool dirty)const noexcept{return dirty&&now&&(force||!last||(now>=last&&now-last>=Interval));}
 void submitted(Ns now)noexcept{last=now;force=false;}
 int waitMs(Ns now,bool dirty)const noexcept;
};
struct SessionStats {std::uint64_t samples{},submits{},contacts{},released{},penContacts{},eraserContacts{};bool failed{},cancelled{};PressureEvidence pressure{};Ns windowStart{};std::uint64_t windowSamples{},windowContacts{};const char* failure{};};
struct OperationSink {
 virtual bool reserve()noexcept=0;
 virtual bool append(const Point&)noexcept=0;
 virtual void cancel()noexcept=0;
 virtual ~OperationSink()=default;
};
struct InputGate {bool open{true},blocked{};Ns notBefore{};std::uint64_t rejected{};};
// The Transform of a new Engine: the marker's mapping onto the surface, with the display's input frame. Only the frame
// (turn and offset) is the display's; axes, size, rotation and mirror stay the marker's.
Transform engineTransform(const Transform& marker,SessionDisplay&)noexcept;
bool consumeEvents(std::span<const input_event>,FrameAssembler&,Engine&,Scheduler&,SessionStats&,Ns,RecoveryLease* =nullptr,OperationSink* =nullptr,InputGate* =nullptr,SessionDisplay* =nullptr)noexcept;
int runDirectSession(RecoveryLease&,SessionDisplay&)noexcept;
}
