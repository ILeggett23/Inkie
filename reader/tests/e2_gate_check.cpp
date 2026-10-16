// SPDX-License-Identifier: AGPL-3.0-only
// E2 provisional gate check (VM only): loads <appRoot>/config/<build's profile name>
// through the shipping loadPinned() and prints the outcome. Built once as a normal
// build pinned to a provisional profile (must refuse) and once as the E2 build
// (must accept only a provisional profile).
#include "qualified_pin_v2.h"
#include <cstdio>
int main(int argc,char** argv){
 if(argc!=2)return 64;
 const auto l=recovery::v2::runtime::loadPinned(argv[1]);
 std::printf("E2_GATE build=%s path=%s loaded=%d failure=%u qualification=%s\n",recovery::v2::e2::provisional?"provisional":"normal",
  recovery::v2::runtime::qualifiedPath(argv[1]).c_str(),bool(l.profile),unsigned(l.failure),l.profile?l.profile->qualification.c_str():"-");
 return l.profile?0:1;
}
