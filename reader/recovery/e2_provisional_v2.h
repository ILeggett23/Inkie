// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// E2 phase-E timing build (outputs/E2-PROCEDURE.md). Only a build configured with
// -DINKBOUND_E2_PROVISIONAL=ON defines INKBOUND_E2_PROVISIONAL: it loads ONLY a
// profile whose qualification starts "provisional-e2" from its own file name, logs
// monotonic stage stamps, and embeds a marker the offline packager refuses. A normal
// build refuses any provisional profile, so a provisional one can never be pinned.
#include <cstdio>
#include <ctime>
namespace recovery::v2::e2 {
inline constexpr const char* ProvisionalPrefix="provisional-e2";
#ifdef INKBOUND_E2_PROVISIONAL
inline constexpr bool provisional=true;
inline void mark(const char* what,unsigned long long value=0)noexcept{
 static const bool announced=(std::fputs("INKBOUND_E2_PROVISIONAL_BUILD never-package\n",stderr),true);(void)announced;
 timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);
 std::fprintf(stderr,"E2T %s mono_us=%llu value=%llu\n",what,(unsigned long long)t.tv_sec*1000000ull+(unsigned long long)t.tv_nsec/1000,value);
}
// Main-loop cadence: largest gap between ticks, reported once per 5 s window.
struct Cadence {
 unsigned long long last{},start{},worst{},ticks{};
 void tick(const char* what)noexcept{
  timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);const unsigned long long now=(unsigned long long)t.tv_sec*1000000ull+(unsigned long long)t.tv_nsec/1000;
  if(last&&now-last>worst)worst=now-last;last=now;++ticks;if(!start)start=now;
  if(now-start>=5000000){std::fprintf(stderr,"E2T %s mono_us=%llu max_gap_us=%llu ticks=%llu\n",what,now,worst,ticks);start=now;worst=0;ticks=0;}
 }
};
#else
inline constexpr bool provisional=false;
inline void mark(const char*,unsigned long long=0)noexcept{}
struct Cadence {void tick(const char*)noexcept{}};
#endif
}
