// SPDX-License-Identifier: AGPL-3.0-only
// Production V2 lifecycle roles. Every role first loads the one build-pinned,
// externally qualified profile; without it nothing is reserved, started or stopped.
#include "dispatch_v2.h"
#include "fuse_evidence_v2.h"
#include "qualified_pin_v2.h"
#include "contract.h"
#include "session_power_v2.h"
#include <sys/random.h>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <unistd.h>
using namespace recovery::v2;
namespace {
std::uint64_t entropy(){std::uint64_t v{};while(!v)if(getrandom(&v,sizeof v,0)!=ssize_t(sizeof v))return 0;return v;}
std::string hexSession(){std::uint8_t b[16]{};if(getrandom(b,sizeof b,0)!=ssize_t(sizeof b))return {};std::string s;for(auto c:b){s+="0123456789abcdef"[c>>4];s+="0123456789abcdef"[c&15];}return s;}
bool number(const char* s,std::uint64_t& out){if(!s||!*s)return false;char* end{};errno=0;auto v=std::strtoull(s,&end,10);if(errno||*end||!v)return false;out=v;return true;}
int refuse(const char* why){std::fprintf(stderr,"V2_LIFECYCLE_REFUSED %s\n",why);return 78;}
}
int main(int argc,char** argv){
 if(argc<2)return refuse("role");
 const std::string_view role=argv[1];
 const std::filesystem::path self=recovery::executable(),bin=self.parent_path(),app=bin.parent_path();
 auto loaded=runtime::loadPinned(app.string());
 if(!loaded.profile||!loaded.route)return refuse("no build-pinned qualified lifecycle profile");
 const auto& profile=*loaded.profile;
 if(role=="--v2-launch"&&argc==2){
  const auto session=hexSession();const auto generation=entropy(),epoch=entropy();
  if(session.empty()||!generation||!epoch)return refuse("entropy");
  const std::vector<std::string> command{self.string(),"--v2-owner",session,std::to_string(getpid()),std::to_string(generation),std::to_string(epoch)};
  auto started=dispatch::startOwner(profile,session,command);if(!started)return refuse("owner unit start");
  return dispatch::launch(profile,session,started->pid,started->invocation,generation,epoch)==dispatch::Launch::Committed?0:78;
 }
 if(role=="--v2-owner"&&argc==6){
  const char* env=getenv("INKBOUND_RECOVERY_V2_SESSION");std::uint64_t launcher{},generation{},epoch{};
  if(!env||std::string_view(env)!=argv[2]||!number(argv[3],launcher)||launcher>0x7fffffffULL||!number(argv[4],generation)||!number(argv[5],epoch))return refuse("owner arguments");
  dispatch::OwnerPlan plan;plan.roles=profile;plan.manager=profile;plan.session=argv[2];plan.stock="xochitl.service";
  if(!loaded.sessionPower)return refuse("no session power configuration");
  plan.powerKey=sessionpower::findInput("/sys","/dev/input",loaded.sessionPower->powerKey).string();if(plan.powerKey.empty())return refuse("owner power key not uniquely found");
  plan.reader={(bin/"inkbound-direct-handwriting-spike3").string(),"--v2-reader",plan.session};
  plan.fuse={self.string(),"--v2-fuse",plan.session};plan.witness={self.string(),"--v2-witness"};
  plan.launcherPid=std::uint32_t(launcher);plan.generation=generation;plan.launcherEpoch=epoch;
  // The pinned profile's explicit verified-platform route; no default, no inference.
  recovery::power::Config config;config.route=*loaded.route;config.expectedServiceUid=0;
  dispatch::EventsPower power(config);auto r=dispatch::runOwner(plan,power);
  std::fprintf(stderr,"V2_OWNER_RESULT outcome=%d committed=%d restored=%d released=%d report_bound=%d report_durable=%d closure=%u report_detail=%s stage=%s\n",int(r.outcome),r.committed,r.restored,r.released,r.reportBound,r.reportDurable,unsigned(r.closure),r.reportDetail.empty()?"-":r.reportDetail.c_str(),r.stage.c_str());
  return r.outcome==dispatch::Outcome::Restored||r.outcome==dispatch::Outcome::Aborted?0:78;
 }
 if(role=="--v2-fuse"&&argc==3){dispatch::ManagerFuseEvidence evidence(profile,argv[2],loaded.route.has_value());return dispatch::runFuse(profile,argv[2],evidence);}
 if(role=="--v2-witness"&&argc==5){std::uint64_t generation{},epoch{};if(!number(argv[3],generation)||!number(argv[4],epoch))return refuse("witness arguments");return dispatch::runWitness(profile,argv[2],generation,epoch);}
 return refuse("role");
}
