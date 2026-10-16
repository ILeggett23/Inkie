// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include "wake_sources.h"
#include <filesystem>
#include <string>
#include <string_view>
namespace recovery::v2::sessionpower {
using Ns=std::uint64_t;
// Kernel autosleep is enabled on the target (/sys/power/autosleep=mem) and it
// bypasses logind. While a V2 session is admitted the reader holds a named
// kernel wakeup source with a TIMEOUT, renewed only while admitted: if the
// reader dies or freezes the lock expires on its own within one timeout.
class WakeLock final {
 std::filesystem::path directory_;std::string name_;bool held_{},failed_{};
 bool write(const char* file,const std::string& text)noexcept;
public:
 // powerDirectory is /sys/power in production; tests inject a private directory.
 WakeLock(std::filesystem::path powerDirectory,std::string name)noexcept;
 ~WakeLock();WakeLock(const WakeLock&)=delete;WakeLock& operator=(const WakeLock&)=delete;
 bool hold(Ns timeout)noexcept;   // "<name> <timeout_ns>" -> wake_lock
 bool release()noexcept;          // "<name>" -> wake_unlock (only if held)
 bool held()const noexcept{return held_;}bool failed()const noexcept{return failed_;}
};
// Finds /dev/input/eventN whose device name matches exactly, via sysfs
// (<sysfs>/class/input/eventN/device/name). Numbering is not stable across boots.
std::filesystem::path findInput(const std::filesystem::path& sysfsRoot,const std::filesystem::path& devRoot,const std::string& name)noexcept;
// Non-grabbing readers of the power key and cover (lid) switch, plus an idle
// timeout. Any of them requests an orderly session end into stock.
class EndSignals final {
 int power_{-1},cover_{-1};Ns idle_{},last_{};bool requested_{};
 static bool drain(int fd,bool cover)noexcept;
public:
 EndSignals(const std::filesystem::path& powerKey,const std::filesystem::path& cover,Ns idleTimeout,Ns now)noexcept;
 ~EndSignals();EndSignals(const EndSignals&)=delete;EndSignals& operator=(const EndSignals&)=delete;
 bool valid()const noexcept{return power_>=0&&idle_;}
 void activity(Ns now)noexcept{if(now>last_)last_=now;}
 // Non-blocking; sticky once true.
 bool endRequested(Ns now)noexcept;
 // Owner backstop: power presses since the last call, not sticky (the owner classifies wake presses itself).
 enum class Press {None,Pressed,Lost};
 Press takePress()noexcept;
};
// Suspend detection: CLOCK_BOOTTIME advances while the system is suspended, CLOCK_MONOTONIC does not, so
// their difference only grows by suspended time. Tests inject their own source.
using OffsetSource=Ns(*)(void*)noexcept;
Ns suspendOffset(void*)noexcept;
// "/sys/power/wake_lock" lists the active user-space wakeup sources separated by whitespace.
bool listedWakeLock(std::string_view contents,std::string_view name)noexcept;
// The reader's session wakeup source name, and whether it is active now (unreadable attribute: assumed held, so a
// press keeps its old meaning: end the session).
inline constexpr const char* ReaderWakeLock="inkbound-reader";
bool wakeLockActive(const std::filesystem::path& powerDirectory,std::string_view name)noexcept;
// Owner backstop for D2 power presses. A press normally ends the session: if the reader has not exited after
// drainLead, the owner stops it. A press while the reader's wake lock is released (asleep) or within WakeGrace of a
// resume may be the wake itself: it still starts the backstop, which is cancelled only if the reader then holds its
// lock again (alive and awake). A frozen reader never does, so it is still stopped.
class OwnerPresses final {
 Ns pressedAt_{},lastResume_{},offset_{};bool candidate_{};
public:
 enum class Event {None,Pressed,PressedAsleep,WakeConfirmed};
 explicit OwnerPresses(Ns offset)noexcept:offset_(offset){}
 // Each loop: the current boottime-monotonic offset, whether a press (or a lost key) was read, and the lock state.
 Event step(Ns now,Ns offset,bool pressed,bool lockHeld)noexcept;
 Ns pressedAt()const noexcept{return pressedAt_;}
 bool resumed(Ns now)const noexcept;
};
// The e-paper regulator keeps VPDD on, and holds a kernel wakeup source, for vpdd_length ms after each display
// update (30000 on the Paper Pro), which delays autosleep by up to 30 s after the cover closes. For a sleep the
// value is saved and shortened; waking or ending the session writes the saved value back. Existing attribute only.
inline constexpr const char* DisplayRailLength="/sys/bus/i2c/drivers/g2194-regulator/1-0048/vpdd_length";
class DisplayRail final {
 std::filesystem::path length_;std::string saved_;bool shortened_{};
public:
 explicit DisplayRail(std::filesystem::path lengthFile)noexcept:length_(std::move(lengthFile)){}
 ~DisplayRail(){restore();}DisplayRail(const DisplayRail&)=delete;DisplayRail& operator=(const DisplayRail&)=delete;
 bool shorten(unsigned ms)noexcept;  // saves the current value once, then writes ms; false (unchanged) on any error
 bool restore()noexcept;             // writes the saved value back if shortened
 bool shortened()const noexcept{return shortened_;}
};
// D2 sleep (Inkbound's settings): the cover closing or an idle timeout asks for sleep; the caller quiesces and then
// calls sleep(), which releases the session wake lock so kernel autosleep suspends the tablet. While asleep only
// the power key, cover opening or an admitted explicit input wake takes the lock again. A power
// press while awake ends the session, except a press within WakeGrace of a resume, which was the wake itself.
enum class PowerState {Awake,SleepPending,Sleeping};
enum class SleepReason {None,Cover,Idle};
struct SleepConfig {bool cover{};Ns idle{}; /* 0: never */};
using LidQuery=int(*)(int,void*)noexcept;
int readLid(int,void*)noexcept;
using SwitchQuery=int(*)(int,unsigned,void*)noexcept;int readSwitch(int,unsigned,void*)noexcept;
class SessionPower final {
 NativeWakeSources nativeWake_;SwitchQuery switchQuery_{readSwitch};void* switchContext_{};bool penKnown_{},penDocked_{};WakeCause wakeCause_{WakeCause::None};WakeLock lock_;int power_{-1},cover_{-1};bool coverWanted_{},lidKnown_{},lidClosed_{},lidDropped_{};LidQuery lidQuery_{readLid};void* lidContext_{};unsigned lidQueries_{},lidQueryFailures_{},lidDrops_{};
 Ns timeout_{},renewed_{},last_{},offset_{},resumedAt_{};
 OffsetSource offsetSource_{};void* offsetContext_{};DisplayRail* rail_{};unsigned railShortenings_{},railFailures_{};
 SleepConfig config_{};PowerState state_{PowerState::Awake};SleepReason reason_{SleepReason::None};
 bool end_{},wake_{},resumed_{};unsigned wakes_{},sleeps_{},swallowed_{};
 void wakeUp(Ns now)noexcept;
 void penState(int,Ns,bool)noexcept;void refreshPen(Ns,bool)noexcept;void readInputs(Ns now)noexcept;void coverState(int,Ns,bool edge=false)noexcept;void refreshCover(Ns)noexcept;
public:
 static constexpr Ns WakeGrace=2000000000ull,ResumeThreshold=50000000ull;
 SessionPower(std::filesystem::path powerDirectory,std::string lockName,Ns lockTimeout,
              const std::filesystem::path& powerKey,const std::filesystem::path& cover,Ns now,
              OffsetSource offset=suspendOffset,void* offsetContext=nullptr,LidQuery lidQuery=readLid,void* lidContext=nullptr,SwitchQuery switches=readSwitch,void* switchContext=nullptr,std::filesystem::path sysRoot="/sys")noexcept;
 ~SessionPower();SessionPower(const SessionPower&)=delete;SessionPower& operator=(const SessionPower&)=delete;
 // A cover sleep needs the cover device; without it the cover setting is ignored (power key and idle still work).
 void configure(SleepConfig config,Ns now)noexcept;
 // Reads the power key and cover, detects resume, applies the idle timeout, and renews the lock at most every
 // timeout/4 while admitted and not asleep. Returns false if the lock could not be held (the caller ends the session).
 bool tick(Ns now,bool admitted)noexcept;
 bool endRequested(Ns now)noexcept{if(!end_)readInputs(now);return end_||!valid();}
 // Pen or touch: resets the idle timer and cancels a pending idle sleep; ignored while asleep.
 void activity(Ns now)noexcept;
 PowerState state()const noexcept{return state_;}
 SleepReason reason()const noexcept{return reason_;}
 // SleepPending -> Sleeping: releases the wake lock. The caller has quiesced (or given up waiting) first.
 void prepareSleepDisplay()noexcept;bool sleep(Ns now)noexcept;
 bool inputWake(WakeCause,Ns now)noexcept;WakeCause wakeCause()const noexcept{return wakeCause_;}
 // True once after each wake (the caller may log or redraw).
 bool takeWake()noexcept{bool w=wake_;wake_=false;return w;}
 // True once after each detected resume from suspend.
 bool takeResume()noexcept{bool r=resumed_;resumed_=false;return r;}
 // Optional: shortened for each sleep and restored on wake (see DisplayRail).
 void setDisplayRail(DisplayRail* rail)noexcept{rail_=rail;}
 unsigned railShortenings()const noexcept{return railShortenings_;}unsigned railFailures()const noexcept{return railFailures_;}
 bool coverAvailable()const noexcept{return cover_>=0;}bool lidKnown()const noexcept{return lidKnown_;}bool lidClosed()const noexcept{return lidKnown_&&lidClosed_;}unsigned lidQueryFailures()const noexcept{return lidQueryFailures_;}unsigned lidQueries()const noexcept{return lidQueries_;}unsigned lidDrops()const noexcept{return lidDrops_;}
 unsigned wakes()const noexcept{return wakes_;}unsigned sleeps()const noexcept{return sleeps_;}unsigned swallowedPresses()const noexcept{return swallowed_;}
 bool release()noexcept{return lock_.release();}
 bool valid()const noexcept{return power_>=0&&timeout_;}
 const WakeLock& lock()const noexcept{return lock_;}
};
}
