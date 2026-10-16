// SPDX-License-Identifier: AGPL-3.0-only
#include "profile_loader_v2.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
namespace recovery::v2::runtime {
namespace {
constexpr std::size_t MaxBytes=4096;
constexpr std::uint32_t K[64]={
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
std::uint32_t rotr(std::uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
void block(std::array<std::uint32_t,8>& h,const std::uint8_t* p){
 std::uint32_t w[64];
 for(unsigned i=0;i<16;++i)w[i]=std::uint32_t(p[4*i])<<24|std::uint32_t(p[4*i+1])<<16|std::uint32_t(p[4*i+2])<<8|p[4*i+3];
 for(unsigned i=16;i<64;++i){auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3),s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
 auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],k=h[7];
 for(unsigned i=0;i<64;++i){auto t1=k+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^(~e&g))+K[i]+w[i],t2=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&c)^(b&c));k=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
 h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=k;
}
bool label(std::string_view s){if(s.empty()||s.size()>128)return false;for(unsigned char c:s)if(c<33||c>126)return false;return true;}
bool inferred(std::string_view s){
 std::string lower(s);for(auto& c:lower)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
 return lower.find("synthetic")!=std::string::npos||lower.find("fixture")!=std::string::npos||lower.find("inferred")!=std::string::npos||lower.find("default")!=std::string::npos;
}
bool number(std::string_view s,std::uint64_t& out){
 if(s.empty()||s.size()>20||(s.size()>1&&s[0]=='0'))return false;
 std::uint64_t v=0;for(char c:s){if(c<'0'||c>'9')return false;auto d=std::uint64_t(c-'0');if(v>(std::numeric_limits<std::uint64_t>::max()-d)/10)return false;v=v*10+d;}
 out=v;return true;
}
struct Lines {
 std::string_view rest;
 bool next(std::string_view key,std::string_view& value){
  auto end=rest.find('\n');if(end==std::string_view::npos)return false;
  auto line=rest.substr(0,end);rest.remove_prefix(end+1);
  if(line.size()<=key.size()||line.substr(0,key.size())!=key||line[key.size()]!='=')return false;
  value=line.substr(key.size()+1);return true;
 }
 bool integer(std::string_view key,std::uint64_t& out){std::string_view v;return next(key,v)&&number(v,out);}
};
}
Digest sha256(std::span<const std::uint8_t> data)noexcept{
 std::array<std::uint32_t,8> h{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 std::size_t n=0;for(;n+64<=data.size();n+=64)block(h,data.data()+n);
 std::array<std::uint8_t,128> tail{};std::size_t left=data.size()-n;if(left)std::memcpy(tail.data(),data.data()+n,left);
 tail[left]=0x80;std::size_t total=left+1+8<=64?64:128;
 const std::uint64_t bits=std::uint64_t(data.size())*8;for(unsigned i=0;i<8;++i)tail[total-1-i]=std::uint8_t(bits>>(8*i));
 for(std::size_t i=0;i<total;i+=64)block(h,tail.data()+i);
 Digest out{};for(unsigned i=0;i<8;++i)for(unsigned j=0;j<4;++j)out[4*i+j]=std::uint8_t(h[i]>>(24-8*j));
 return out;
}
namespace {
bool deviceName(std::string_view s){if(s.empty()||s.size()>80||s.front()==' '||s.back()==' ')return false;for(unsigned char c:s)if(c<32||c>126)return false;return true;}
bool parseAll(std::string_view text,Profile& out,power::RouteEvidence& route,SustainedQuota& quota,SessionPowerConfig& session)noexcept{
 try{
  if(text.empty()||text.size()>MaxBytes||text.back()!='\n'||text.find('\0')!=std::string_view::npos||text.find('\r')!=std::string_view::npos)return false;
  Lines in{text};std::string_view v;Profile p;
  if(!in.next("format",v)||v!="inkbound-lifecycle-profile-1")return false;
  if(!in.next("selection",v)||v!="qualified")return false;
  p.selection=Selection::Qualified;
  if(!in.next("qualification",v)||!label(v)||inferred(v))return false;p.qualification=std::string(v);
  if(!in.next("managerVersion",v)||!label(v))return false;p.managerVersion=std::string(v);
  if(!in.next("powerRoutes",v)||!label(v)||inferred(v))return false;p.powerRoutes=std::string(v);
  auto& t=p.timing;std::uint64_t workers{};
  if(!in.integer("healthCadence",t.healthCadence)||!in.integer("challengeLifetime",t.challengeLifetime)||!in.integer("healthMaxAge",t.healthMaxAge)||
     !in.integer("grantLifetime",t.grantLifetime)||!in.integer("drainLead",t.drainLead)||!in.integer("startupBudget",t.startupBudget)||
     !in.integer("stopGrace",t.stopGrace)||!in.integer("killGrace",t.killGrace)||!in.integer("proofWindow",t.proofWindow)||
     !in.integer("sleepAllowance",t.sleepAllowance)||!in.integer("clockSkew",t.clockSkew)||!in.integer("workers",workers))return false;
  if(!workers||workers>MaxWorkers)return false;t.workers=std::uint32_t(workers);
  for(std::uint32_t n=0;n<t.workers;++n){
   const std::string key="worker"+std::to_string(n);if(!in.next(key,v))return false;
   auto first=v.find(','),second=first==std::string_view::npos?first:v.find(',',first+1);
   if(second==std::string_view::npos||v.find(',',second+1)!=std::string_view::npos)return false;
   auto kind=v.substr(0,first);std::uint64_t backlog{},age{};
   if(!number(v.substr(first+1,second-first-1),backlog)||!number(v.substr(second+1),age)||backlog>std::numeric_limits<std::uint32_t>::max())return false;
   auto& policy=t.workerPolicies[n];
   if(kind=="durable-prefix")policy.kind=WorkerKind::DurablePrefix;else if(kind=="cancelable-helper-prefix")policy.kind=WorkerKind::CancelableHelperPrefix;else return false;
   policy.maxBacklog=std::uint32_t(backlog);policy.maxPendingAge=age;
  }
  if(!in.integer("fuseBootstrapLifetime",p.fuse.bootstrapLifetime)||!in.integer("fuseLeaseLifetime",p.fuse.leaseLifetime)||!in.integer("fuseDispatchMargin",p.fuse.dispatchMargin)||
     !in.integer("readerCeiling",p.readerCeiling)||!in.integer("ownerCeiling",p.ownerCeiling)||!in.integer("callBudget",p.callBudget)||
     !in.integer("startBudget",p.startBudget)||!in.integer("stopBudget",p.stopBudget))return false;
  // Explicit platform sleep-route evidence: only verified-platform, all routes, finite barrier.
  std::uint64_t all{},finite{};
  if(!in.next("powerScope",v)||v!="verified-platform")return false;route.scope=power::Scope::VerifiedPlatform;
  if(!in.next("powerEvidence",v)||!label(v)||inferred(v))return false;route.evidenceId=std::string(v);
  if(!in.integer("powerAllRelevantSleepRoutes",all)||all!=1||!in.integer("powerFinitePreSleepBarrier",finite)||finite!=1)return false;
  route.allRelevantSleepRoutes=route.finitePreSleepBarrier=true;
  if(!in.integer("powerDelayBudgetUsec",route.delayBudgetUsec)||!in.integer("powerMaximumSignalLagUsec",route.maximumSignalLagUsec))return false;
  // Sustained-writing quotas: bounded fixed windows; never a default.
  std::uint64_t windowUsec{},contacts{},samples{};
  if(!in.integer("sustainedWindowUsec",windowUsec)||!in.integer("sustainedContactsPerWindow",contacts)||!in.integer("sustainedSamplesPerWindow",samples))return false;
  // Reader session power: power key and cover device names ("-" = no cover), bounded idle timeout.
  std::uint64_t idleUsec{};
  if(!in.next("sessionPowerKeyName",v)||!deviceName(v))return false;session.powerKey=std::string(v);
  if(!in.next("sessionCoverName",v)||!(v=="-"||deviceName(v)))return false;session.cover=v=="-"?std::string{}:std::string(v);
  if(!in.integer("sessionIdleUsec",idleUsec)||idleUsec<10000000||idleUsec>3600000000ull||!in.rest.empty())return false;
  session.idleNs=idleUsec*1000;
  if(windowUsec<100000||windowUsec>60000000||!contacts||contacts>1000||samples<contacts||samples>1000000)return false;
  quota={windowUsec*1000,std::uint32_t(contacts),std::uint32_t(samples)};
  if(!route.delayBudgetUsec||route.maximumSignalLagUsec>=route.delayBudgetUsec)return false;
  if(!valid(p)||p.selection!=Selection::Qualified)return false;
  out=std::move(p);return true;
 }catch(...){return false;}
}
}
std::optional<Profile> parseQualified(std::string_view text)noexcept{Profile p;power::RouteEvidence r;SustainedQuota q;SessionPowerConfig c;if(!parseAll(text,p,r,q,c))return {};return p;}
std::optional<power::RouteEvidence> parseRoute(std::string_view text)noexcept{Profile p;power::RouteEvidence r;SustainedQuota q;SessionPowerConfig c;if(!parseAll(text,p,r,q,c))return {};return r;}
std::optional<SustainedQuota> parseSustained(std::string_view text)noexcept{Profile p;power::RouteEvidence r;SustainedQuota q;SessionPowerConfig c;if(!parseAll(text,p,r,q,c))return {};return q;}
std::optional<SessionPowerConfig> parseSessionPower(std::string_view text)noexcept{Profile p;power::RouteEvidence r;SustainedQuota q;SessionPowerConfig c;if(!parseAll(text,p,r,q,c))return {};return c;}
Loaded loadQualified(const char* path,const Digest& pinned,uid_t expectedOwner)noexcept{
 Loaded result;
 if(pinned==Digest{}){result.failure=LoadFailure::Unpinned;return result;}
 if(!path||path[0]!='/'){result.failure=LoadFailure::Open;return result;}
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK|O_NOCTTY);
 if(fd<0){result.failure=LoadFailure::Open;return result;}
 struct Close{int fd;~Close(){close(fd);}} held{fd};
 struct stat st{};
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)){result.failure=LoadFailure::NotRegular;return result;}
 if(st.st_uid!=expectedOwner){result.failure=LoadFailure::Owner;return result;}
 if(st.st_mode&0022){result.failure=LoadFailure::Mode;return result;}
 if(st.st_nlink!=1){result.failure=LoadFailure::Links;return result;}
 if(st.st_size<1||std::size_t(st.st_size)>MaxBytes){result.failure=LoadFailure::Size;return result;}
 std::array<std::uint8_t,MaxBytes+1> bytes{};std::size_t size=0;
 for(;;){auto n=read(fd,bytes.data()+size,bytes.size()-size);if(n<0&&errno==EINTR)continue;if(n<0){result.failure=LoadFailure::Read;return result;}if(!n)break;size+=std::size_t(n);if(size>MaxBytes){result.failure=LoadFailure::Size;return result;}}
 if(size!=std::size_t(st.st_size)){result.failure=LoadFailure::Read;return result;}
 auto digest=sha256({bytes.data(),size});std::uint8_t diff=0;for(std::size_t i=0;i<digest.size();++i)diff|=std::uint8_t(digest[i]^pinned[i]);
 if(diff){result.failure=LoadFailure::Digest;return result;}
 std::string_view text(reinterpret_cast<const char*>(bytes.data()),size);
 auto profile=parseQualified(text);
 if(!profile){
  // Distinguish refusal classes for diagnostics only; none selects a profile.
  result.failure=text.find("selection=qualified\n")==std::string_view::npos?LoadFailure::Selection:inferred(text)?LoadFailure::Inferred:LoadFailure::Format;
  return result;
 }
 result.route=parseRoute(text);result.sustained=parseSustained(text);result.sessionPower=parseSessionPower(text);if(!result.route||!result.sustained||!result.sessionPower){result.failure=LoadFailure::Format;return result;}
 result.profile=std::move(profile);result.failure=LoadFailure::None;return result;
}
}
