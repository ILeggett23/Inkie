// SPDX-License-Identifier: AGPL-3.0-only
#include "fuse_evidence_v2.h"
#include "contract.h"
#include <systemd/sd-bus.h>
#include <stdexcept>
#include <time.h>
#include <unistd.h>
namespace recovery::v2::dispatch {
namespace {
constexpr const char* Systemd="org.freedesktop.systemd1";
constexpr const char* ManagerPath="/org/freedesktop/systemd1";
constexpr const char* ManagerIface="org.freedesktop.systemd1.Manager";
constexpr const char* UnitIface="org.freedesktop.systemd1.Unit";
constexpr const char* ServiceIface="org.freedesktop.systemd1.Service";
constexpr const char* Login="org.freedesktop.login1";
void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
Ns monotonicNow(){timespec t{};need(!clock_gettime(CLOCK_MONOTONIC,&t),"fuse evidence clock");return Ns(t.tv_sec)*1000000000+Ns(t.tv_nsec);}
struct Error {sd_bus_error e=SD_BUS_ERROR_NULL;~Error(){sd_bus_error_free(&e);}};
struct Message {sd_bus_message* m{};~Message(){sd_bus_message_unref(m);}};
}
struct ManagerFuseEvidence::Impl {
 runtime::Profile profile;std::string session;bool route{};
 sd_bus* bus{};std::string systemdOwner,loginOwner,boot;
 bool initialized{},failed{};Ns next{};fuse::Evidence cached{};
 fuse::Binding binding{};SessionIdentity identity{};std::array<std::string,3> names,paths;std::array<std::uint32_t,3> pids{};std::array<std::string,3> invocations;
 ~Impl(){sd_bus_flush_close_unref(bus);}
 std::string owner(const char* name){Error e;Message m;const char* o{};
  need(sd_bus_call_method(bus,"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","GetNameOwner",&e.e,&m.m,"s",name)>=0&&sd_bus_message_read(m.m,"s",&o)>0&&o,"bus owner");
  std::uint32_t uid{};Error u;Message um;need(sd_bus_call_method(bus,"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","GetConnectionUnixUser",&u.e,&um.m,"s",o)>=0&&sd_bus_message_read(um.m,"u",&uid)>0&&uid==0,"root bus service");
  return o;}
 std::string unitPath(const std::string& name){Error e;Message m;const char* p{};need(sd_bus_call_method(bus,systemdOwner.c_str(),ManagerPath,ManagerIface,"GetUnit",&e.e,&m.m,"s",name.c_str())>=0&&sd_bus_message_read(m.m,"o",&p)>0&&p,"unit path");return p;}
 std::string text(const std::string& path,const char* iface,const char* key){Error e;char* v{};need(sd_bus_get_property_string(bus,systemdOwner.c_str(),path.c_str(),iface,key,&e.e,&v)>=0&&v,key);std::string out(v);free(v);return out;}
 template<class T>T trivial(const std::string& path,const char* iface,const char* key,char type){Error e;T v{};need(sd_bus_get_property_trivial(bus,systemdOwner.c_str(),path.c_str(),iface,key,&e.e,type,&v)>=0,key);return v;}
 std::vector<std::string> strings(const std::string& path,const char* iface,const char* key){Error e;char** v{};need(sd_bus_get_property_strv(bus,systemdOwner.c_str(),path.c_str(),iface,key,&e.e,&v)>=0,key);std::vector<std::string> out;for(auto p=v;p&&*p;++p){out.emplace_back(*p);free(*p);}free(v);return out;}
 std::string invocation(const std::string& path){Error e;Message m;need(sd_bus_get_property(bus,systemdOwner.c_str(),path.c_str(),UnitIface,"InvocationID",&e.e,&m.m,"ay")>=0,"invocation");const void* bytes{};std::size_t count{};need(sd_bus_message_read_array(m.m,'y',&bytes,&count)>0&&count==16,"invocation bytes");std::string out;for(std::size_t n=0;n<count;++n){auto b=static_cast<const unsigned char*>(bytes)[n];out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
 runtime::UnitProfile observed(const std::string& path){runtime::UnitProfile p;
  p.type=text(path,ServiceIface,"Type");p.notifyAccess=text(path,ServiceIface,"NotifyAccess");p.restart=text(path,ServiceIface,"Restart");p.killMode=text(path,ServiceIface,"KillMode");
  p.runtimeUsec=trivial<std::uint64_t>(path,ServiceIface,"RuntimeMaxUSec",'t');p.startUsec=trivial<std::uint64_t>(path,ServiceIface,"TimeoutStartUSec",'t');
  p.stopUsec=trivial<std::uint64_t>(path,ServiceIface,"TimeoutStopUSec",'t');p.abortUsec=trivial<std::uint64_t>(path,ServiceIface,"TimeoutAbortUSec",'t');
  p.capabilities=trivial<std::uint64_t>(path,ServiceIface,"CapabilityBoundingSet",'t');p.killSignal=trivial<std::int32_t>(path,ServiceIface,"KillSignal",'i');
  p.finalKillSignal=trivial<std::int32_t>(path,ServiceIface,"FinalKillSignal",'i');p.sendSigkill=trivial<int>(path,ServiceIface,"SendSIGKILL",'b');
  p.noNewPrivileges=trivial<int>(path,ServiceIface,"NoNewPrivileges",'b');p.delegate=trivial<int>(path,ServiceIface,"Delegate",'b');
  p.bindsTo=strings(path,UnitIface,"BindsTo");p.after=strings(path,UnitIface,"After");return p;}
 void initialize(const fuse::Binding& b,const SessionIdentity& id){
  need(runtime::valid(profile)&&validIdentity(id)&&std::string(id.session.data())==session,"fuse evidence identity");
  binding=b;identity=id;boot=recovery::boot();need(!boot.empty(),"boot id");
  need(sd_bus_open_system(&bus)>=0,"fuse system bus");systemdOwner=owner(Systemd);loginOwner=owner(Login);
  names={id.owner.data(),id.unit.data(),b.unit.data()};pids={id.ownerPid,id.mainPid,std::uint32_t(getpid())};
  invocations={id.ownerInvocation.data(),id.invocation.data(),b.invocation.data()};
  const runtime::Role roles[3]={runtime::Role::Owner,runtime::Role::Reader,runtime::Role::Fuse};
  for(std::size_t n=0;n<3;++n){
   paths[n]=unitPath(names[n]);
   // The fuse's own reference on each exact unit, held by this connection.
   Error e;Message m;need(sd_bus_call_method(bus,systemdOwner.c_str(),ManagerPath,ManagerIface,"RefUnit",&e.e,&m.m,"s",names[n].c_str())>=0,"fuse RefUnit");
   need(runtime::matches(runtime::requested(profile,roles[n],names[0],names[2]),observed(paths[n])),"typed unit profile");
  }
  initialized=true;
 }
 bool refresh(){
  need(bus&&sd_bus_is_open(bus)>0&&recovery::boot()==boot,"manager continuity");
  for(unsigned n=0;n<64&&sd_bus_process(bus,nullptr)>0;++n){}
  need(owner(Systemd)==systemdOwner&&owner(Login)==loginOwner,"bus owner continuity");
  for(std::size_t n=0;n<3;++n){
   need(unitPath(names[n])==paths[n]&&invocation(paths[n])==invocations[n]&&trivial<std::uint32_t>(paths[n],ServiceIface,"MainPID",'u')==pids[n],"original unit continuity");
   const auto active=text(paths[n],UnitIface,"ActiveState");need(active=="active"||(n==2&&active=="activating"),"unit active");
  }
  Error e;int preparing{};need(sd_bus_get_property_trivial(bus,loginOwner.c_str(),"/org/freedesktop/login1","org.freedesktop.login1.Manager","PreparingForSleep",&e.e,'b',&preparing)>=0,"login1 sleep state");
  cached={binding,identity,true,true,true,true,true,route&&!preparing};
  return true;
 }
};
ManagerFuseEvidence::ManagerFuseEvidence(runtime::Profile p,std::string s,bool routeQualified)noexcept{
 try{impl_=std::make_unique<Impl>();impl_->profile=std::move(p);impl_->session=std::move(s);impl_->route=routeQualified;}catch(...){impl_.reset();}
}
ManagerFuseEvidence::~ManagerFuseEvidence()=default;
bool ManagerFuseEvidence::evidence(const fuse::Binding& b,const SessionIdentity& id,fuse::Evidence& out)noexcept{
 if(!impl_||impl_->failed)return false;
 try{
  auto& i=*impl_;
  if(!i.initialized)i.initialize(b,id);
  need(b==i.binding&&id==i.identity,"fuse evidence binding changed");
  // Bounded cadence: at most one manager round trip per half health cadence.
  const Ns now=monotonicNow();if(now>=i.next){i.refresh();i.next=now+i.profile.timing.healthCadence/2;}
  out=i.cached;return true;
 }catch(...){impl_->failed=true;return false;}
}
}
