// SPDX-License-Identifier: AGPL-3.0-only
// E2 supervised on-device measurement probes (see outputs/E2-PROCEDURE.md).
// Never packaged. Each subcommand writes only inside its phase directory under
// /home/root/inkbound-e2/, except the ONE kernel wake-lock attribute in `wake`
// and the ONE stop + ONE start of the named stock unit in `timing`.
//   e2-probe wake   <outdir> [--sysfs <root>] [--wait-ms N]
//   e2-probe input  <outdir> <seconds> <power-key-name> <cover-name|-> [--sysfs <root> --dev <root>]
//   e2-probe timing <outdir> <unit> --confirm-stock-mutation [--vm-test-outdir 1]
//   e2-probe profile-check <file>   (local: parse with the shipping parser, print SHA-256)
#include "session_power_v2.h"
#include "dispatch_v2.h"
#include "profile_loader_v2.h"
#include "e2_provisional_v2.h"
#include <linux/input.h>
#include <systemd/sd-bus.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <filesystem>
#include <vector>
namespace fs=std::filesystem;
using recovery::v2::sessionpower::Ns;
namespace {
constexpr const char* Base="/home/root/inkbound-e2/";
Ns mono(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return Ns(t.tv_sec)*1000000000+Ns(t.tv_nsec);}
std::string slurp(const fs::path& p){std::ifstream in(p);std::stringstream s;s<<in.rdbuf();return s.str();}
// Output directories are private and must be under the E2 base on the device.
bool outdir(const fs::path& d,bool test){
 const auto s=d.lexically_normal().string();
 if(!test&&(s.rfind(Base,0)!=0||s.find("..")!=std::string::npos))return false;
 std::error_code e;fs::create_directories(d,e);return !e&&fs::is_directory(d)&&!fs::is_symlink(d);
}
std::string argvLine;
struct Log {std::ofstream out;explicit Log(const fs::path& p):out(p,std::ios::app){out<<"ARGV "<<argvLine<<"\n";}
 template<class...A>void line(const char* f,A... a){char b[512];std::snprintf(b,sizeof b,f,a...);out<<b<<"\n";out.flush();std::printf("%s\n",b);}};
// Wakeup source state. Kernel >= 5.4 exposes every source (including user-space
// wake_lock ones) as <sysfs>/class/wakeup/wakeupN/{name,active_count,...};
// active_time_ms is non-zero only while the source is currently active.
struct Source {bool found{};unsigned long long active{},activeMs{},expired{};std::string where;};
unsigned long long number(const fs::path& p){std::ifstream in(p);unsigned long long v{};in>>v;return v;}
Source source(const fs::path& sysfs,const std::string& name){
 Source s;std::error_code e;
 for(fs::directory_iterator i(sysfs/"class/wakeup",e),end;!e&&i!=end;i.increment(e)){
  std::ifstream in(i->path()/"name");std::string n;if(!std::getline(in,n)||n!=name)continue;
  s.found=true;s.where=i->path().string();s.active=number(i->path()/"active_count");s.activeMs=number(i->path()/"active_time_ms");s.expired=number(i->path()/"expire_count");return s;
 }
 return s;
}
std::string show(const Source& s){char b[256];std::snprintf(b,sizeof b,"found=%d active_count=%llu active_time_ms=%llu expire_count=%llu at=%s",s.found,s.active,s.activeMs,s.expired,s.where.c_str());return b;}
int wake(const fs::path& out,const fs::path& sysfs,bool test,unsigned waitMs){
 if(!outdir(out,test))return 64;Log log(out/"wake.txt");const std::string name="inkbound-e2";
 if(test)log.line("SYSFS=SIMULATED %s",sysfs.c_str());
 const auto autosleep=slurp(sysfs/"power/autosleep");log.line("AUTOSLEEP_BEFORE=%s",autosleep.substr(0,autosleep.find('\n')).c_str());
 const auto before=source(sysfs,name);log.line("SOURCE_BEFORE %s",show(before).c_str());
 if(before.found&&before.activeMs){log.line("E2_WAKE_REFUSED source already active");return 2;}
 recovery::v2::sessionpower::WakeLock lock(sysfs/"power",name);
 const Ns t0=mono();if(!lock.hold(5000000000ull)){log.line("E2_WAKE_FAIL hold");return 1;}
 usleep(200000);
 const auto during=source(sysfs,name);log.line("SOURCE_DURING_MS=%llu %s",(unsigned long long)((mono()-t0)/1000000),show(during).c_str());
 const bool activeDuring=during.found&&during.activeMs>0;
 // Deliberately not released: the timed lock must expire by itself.
 const Ns until=t0+Ns(waitMs)*1000000;while(mono()<until)usleep(100000);
 const auto after=source(sysfs,name);
 log.line("SOURCE_AFTER_MS=%llu %s",(unsigned long long)((mono()-t0)/1000000),show(after).c_str());
 const bool expired=after.found&&after.activeMs==0&&after.expired>before.expired;
 const auto autosleepAfter=slurp(sysfs/"power/autosleep");
 lock.release(); // clean-up write of the (already inactive) source name to wake_unlock
 const bool unchanged=autosleep==autosleepAfter;
 const bool ok=activeDuring&&expired&&unchanged;
 log.line("%s active_during=%d expired_by_itself=%d autosleep_unchanged=%d",ok?"E2_WAKE_PASS":"E2_WAKE_FAIL",activeDuring,expired,unchanged);
 return ok?0:1;
}
int input(const fs::path& out,unsigned seconds,const std::string& keyName,const std::string& coverName,const fs::path& sysfs,const fs::path& dev,bool test){
 if(!outdir(out,test)||!seconds||seconds>600)return 64;Log log(out/"input.txt");
 const auto key=recovery::v2::sessionpower::findInput(sysfs,dev,keyName);
 const auto cover=coverName=="-"?fs::path{}:recovery::v2::sessionpower::findInput(sysfs,dev,coverName);
 log.line("POWER_KEY=%s COVER=%s",key.c_str(),cover.c_str());if(key.empty()||(coverName!="-"&&cover.empty())){log.line("E2_INPUT_FAIL lookup");return 1;}
 int fds[2]={::open(key.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW),cover.empty()?-1: ::open(cover.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW)};
 if(fds[0]<0||(!cover.empty()&&fds[1]<0)){log.line("E2_INPUT_FAIL open");return 1;}
 bool press=false,lid=false;const Ns end=mono()+Ns(seconds)*1000000000;
 while(mono()<end){
  if(fds[0]<0&&fds[1]<0)break;
  pollfd p[2]={{fds[0],POLLIN,0},{fds[1],POLLIN,0}};int n=poll(p,2,200);if(n<=0)continue;
  for(int i=0;i<2;++i){if(fds[i]<0)continue;
   if(!(p[i].revents&POLLIN)){if(p[i].revents&(POLLHUP|POLLERR|POLLNVAL)){log.line("DEVICE_LOST dev=%s",i?"cover":"key");::close(fds[i]);fds[i]=-1;}continue;}
   input_event e{};
   while(::read(fds[i],&e,sizeof e)==ssize_t(sizeof e)){if(e.type==EV_SYN)continue;
    log.line("EVENT dev=%s type=%u code=%u value=%d",i?"cover":"key",unsigned(e.type),unsigned(e.code),e.value);
    if(!i&&e.type==EV_KEY&&e.code==KEY_POWER&&e.value==1)press=true;if(i&&e.type==EV_SW&&e.code==SW_LID&&e.value==1)lid=true;}}
 }
 for(int f:fds)if(f>=0)::close(f);
 log.line("E2_INPUT_DONE power_press_seen=%d cover_close_seen=%d",press,lid);return 0;
}
struct Bus {sd_bus* b{};~Bus(){sd_bus_flush_close_unref(b);}};
std::string unitPath(sd_bus* b,const std::string& u){sd_bus_message* m{};sd_bus_error e=SD_BUS_ERROR_NULL;const char* o{};std::string p;
 if(sd_bus_call_method(b,"org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","GetUnit",&e,&m,"s",u.c_str())>=0&&sd_bus_message_read(m,"o",&o)>0&&o)p=o;
 sd_bus_message_unref(m);sd_bus_error_free(&e);return p;}
std::string prop(sd_bus* b,const std::string& path,const char* iface,const char* k){char* v{};sd_bus_error e=SD_BUS_ERROR_NULL;std::string out;if(sd_bus_get_property_string(b,"org.freedesktop.systemd1",path.c_str(),iface,k,&e,&v)>=0&&v)out=v;free(v);sd_bus_error_free(&e);return out;}
std::uint32_t prop32(sd_bus* b,const std::string& path,const char* iface,const char* k){std::uint32_t v{};sd_bus_error e=SD_BUS_ERROR_NULL;sd_bus_get_property_trivial(b,"org.freedesktop.systemd1",path.c_str(),iface,k,&e,'u',&v);sd_bus_error_free(&e);return v;}
bool method(sd_bus* b,const char* m,const std::string& u){sd_bus_message* r{};sd_bus_error e=SD_BUS_ERROR_NULL;int rc=sd_bus_call_method(b,"org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager",m,&e,&r,"ss",u.c_str(),"replace");sd_bus_message_unref(r);sd_bus_error_free(&e);return rc>=0;}
int timing(const fs::path& out,const std::string& unit,bool confirmed,bool test){
 if(!outdir(out,test)||!confirmed||unit.empty())return 64;Log log(out/"timing.txt");
 // Start-limit guard first: never be the start that trips StartLimitBurst.
 const auto h=recovery::v2::dispatch::stockStartHeadroom(unit);
 log.line("HEADROOM ok=%d reason=%s interval_us=%llu burst=%u active_for_us=%llu",h.ok,h.reason.c_str(),(unsigned long long)h.intervalUsec,h.burst,(unsigned long long)h.activeForUsec);
 if(!h.ok){log.line("E2_TIMING_REFUSED %s",h.reason.c_str());return 2;}
 Bus bus;if(sd_bus_open_system(&bus.b)<0){log.line("E2_TIMING_FAIL bus");return 1;}
 const auto path=unitPath(bus.b,unit);if(path.empty()){log.line("E2_TIMING_FAIL unit");return 1;}
 const auto restartsBefore=prop32(bus.b,path,"org.freedesktop.systemd1.Service","NRestarts");
 const Ns t0=mono();if(!method(bus.b,"StopUnit",unit)){log.line("E2_TIMING_FAIL stop");return 1;}
 for(;;){const auto a=prop(bus.b,path,"org.freedesktop.systemd1.Unit","ActiveState");if(a=="inactive"&&!prop32(bus.b,path,"org.freedesktop.systemd1.Service","MainPID"))break;
  if(mono()-t0>120*1000000000ull){log.line("E2_TIMING_FAIL stop-bound");return 1;}usleep(5000);}
 const Ns t1=mono();log.line("STOP_MS=%llu",(unsigned long long)((t1-t0)/1000000));
 if(!method(bus.b,"StartUnit",unit)){log.line("E2_TIMING_FAIL start (recovery: one StartUnit only if headroom allows)");return 1;}
 for(;;){const auto a=prop(bus.b,path,"org.freedesktop.systemd1.Unit","ActiveState"),s=prop(bus.b,path,"org.freedesktop.systemd1.Unit","SubState");
  if(a=="active"&&s=="running"&&prop32(bus.b,path,"org.freedesktop.systemd1.Service","MainPID")>1)break;
  if(a=="failed"){log.line("E2_TIMING_FAIL start-failed");return 1;}if(mono()-t1>120*1000000000ull){log.line("E2_TIMING_FAIL start-bound");return 1;}usleep(5000);}
 const Ns t2=mono();const auto restartsAfter=prop32(bus.b,path,"org.freedesktop.systemd1.Service","NRestarts");
 log.line("START_TO_RUNNING_MS=%llu",(unsigned long long)((t2-t1)/1000000));
 log.line("%s nrestarts_before=%u nrestarts_after=%u",restartsBefore==restartsAfter?"E2_TIMING_PASS":"E2_TIMING_FAIL",restartsBefore,restartsAfter);
 return restartsBefore==restartsAfter?0:1;
}
}
int profileCheck(const std::string& path){
 const auto text=slurp(path);namespace rt=recovery::v2::runtime;
 auto p=rt::parseQualified(text);if(!p){std::printf("PROFILE_REFUSED parse\n");return 1;}
 const auto d=rt::sha256({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
 static const char* x="0123456789abcdef";std::string hex;for(auto b:d){hex+=x[b>>4];hex+=x[b&15];}
 const bool provisional=p->qualification.rfind(recovery::v2::e2::ProvisionalPrefix,0)==0;
 std::printf("PROFILE_OK sha256=%s qualification=%s manager=%s provisional=%d\n",hex.c_str(),p->qualification.c_str(),p->managerVersion.c_str(),provisional);return 0;
}
int main(int argc,char** argv){
 std::vector<std::string_view> a(argv+1,argv+argc);if(a.size()<2)return 64;
 for(int i=0;i<argc;++i){argvLine+=argv[i];argvLine+=' ';}std::fprintf(stderr,"E2_ARGV %s\n",argvLine.c_str());
 auto opt=[&](std::string_view k)->std::string{for(std::size_t i=0;i+1<a.size();++i)if(a[i]==k)return std::string(a[i+1]);return {};};
 const std::string sysfs=opt("--sysfs"),dev=opt("--dev");const bool test=!sysfs.empty();
 if(a[0]=="profile-check")return profileCheck(std::string(a[1]));
 if(a[0]=="wake"){const auto w=opt("--wait-ms");return wake(std::string(a[1]),test?fs::path(sysfs):fs::path("/sys"),test,w.empty()?10000u:unsigned(std::stoul(w)));}
 if(a[0]=="input"&&a.size()>=5)return input(std::string(a[1]),unsigned(std::stoul(std::string(a[2]))),std::string(a[3]),std::string(a[4]),test?fs::path(sysfs):fs::path("/sys"),dev.empty()?fs::path("/dev/input"):fs::path(dev),test);
 if(a[0]=="timing"&&a.size()>=3){bool c=false;for(auto x:a)c|=x=="--confirm-stock-mutation";const bool vm=opt("--vm-test-outdir")=="1";return timing(std::string(a[1]),std::string(a[2]),c,vm);}
 return 64;
}
