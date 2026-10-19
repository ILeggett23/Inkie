// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Device status bridge (read-only). Battery state comes from the Paper Pro's fuel gauge in sysfs; the same files
// the E2 device runs read on this tablet (max1726x_battery/status and capacity, power_supply/*/online). Nothing is
// written and nothing is guessed: an unreadable or malformed value is reported as unknown, and the UI then shows
// no battery at all rather than a made-up number. The Wi-Fi link is the kernel's wlan0 operstate (inventory of
// 2026-10-05, firmware 3.27.3.0): "up" once associated. See docs/system-settings-integration.md.
#include <array>
#include <cctype>
#include <cmath>
#include <numbers>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <cstdint>
#include <sys/statvfs.h>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
namespace device {
enum class Charge {Unknown,Discharging,Charging,Full,NotCharging};
struct Battery {std::optional<int> percent;Charge charge{Charge::Unknown};bool external{};bool operator==(const Battery&)const=default;
 bool charging()const noexcept{return charge==Charge::Charging||charge==Charge::Full;}};
namespace detail {
inline std::optional<std::string> line(const std::filesystem::path& file)noexcept{
 try{std::ifstream in(file);if(!in)return std::nullopt;std::string s;s.resize(64);in.read(s.data(),64);s.resize(std::size_t(in.gcount()));
  while(!s.empty()&&std::isspace(static_cast<unsigned char>(s.back())))s.pop_back();if(s.empty()||s.size()>32)return std::nullopt;return s;}catch(...){return std::nullopt;}}
}
inline Battery readBattery(const std::filesystem::path& root="/sys/class/power_supply")noexcept{
 Battery b;const auto gauge=root/"max1726x_battery";
 if(auto v=detail::line(gauge/"capacity")){bool digits=v->size()<=3;for(char c:*v)digits=digits&&std::isdigit(static_cast<unsigned char>(c));if(digits){int n=std::stoi(*v);if(n>=0&&n<=100)b.percent=n;}}
 if(auto v=detail::line(gauge/"status")){if(*v=="Charging")b.charge=Charge::Charging;else if(*v=="Discharging")b.charge=Charge::Discharging;else if(*v=="Full")b.charge=Charge::Full;else if(*v=="Not charging")b.charge=Charge::NotCharging;}
 // External power: any supply other than the gauge reporting online. The Marker's own batteries (nfc-marker-battery,
 // elants-marker-battery on this firmware) also report "online" and say nothing about the tablet's power.
 try{for(const auto& e:std::filesystem::directory_iterator(root)){const auto name=e.path().filename().string();if(name=="max1726x_battery"||name.find("marker")!=std::string::npos)continue;if(auto v=detail::line(e.path()/"online");v&&*v=="1")b.external=true;}}catch(...){}
 return b;
}
// Temperatures (read-only), in degrees C: the panel/ambient sensor the display driver reads (hwmon "rm_hwmon_temp", the
// one Xochitl's temperature module and the vendor waveform choice use), the CPU ("cpu_thermal"), and the battery fuel
// gauge (power_supply max1726x_battery: "temp" in tenths, "health"). The gauge reports health "Overheat" (or "Hot")
// itself when the battery is too warm. Unreadable values stay unknown.
struct Temperature {std::optional<double> panel,cpu,battery;bool batteryHot{};bool operator==(const Temperature&)const=default;};
inline Temperature readTemperature(const std::filesystem::path& hwmon="/sys/class/hwmon",const std::filesystem::path& supply="/sys/class/power_supply")noexcept{
 Temperature t;auto milli=[](const std::filesystem::path& f)->std::optional<double>{auto v=detail::line(f);if(!v)return std::nullopt;try{std::size_t n=0;const long long x=std::stoll(*v,&n);if(n!=v->size()||x<-60000||x>150000)return std::nullopt;return double(x)/1000.;}catch(...){return std::nullopt;}};
 try{for(const auto& e:std::filesystem::directory_iterator(hwmon)){const auto name=detail::line(e.path()/"name");if(!name)continue;if(*name=="rm_hwmon_temp")t.panel=milli(e.path()/"temp1_input");else if(*name=="cpu_thermal")t.cpu=milli(e.path()/"temp1_input");}}catch(...){}
 const auto gauge=supply/"max1726x_battery";
 if(auto v=detail::line(gauge/"temp")){try{std::size_t n=0;const long x=std::stol(*v,&n);if(n==v->size()&&x>-600&&x<1500)t.battery=double(x)/10.;}catch(...){}}
 if(auto v=detail::line(gauge/"health"))t.batteryHot=*v=="Overheat"||*v=="Hot";
 return t;
}
// "Too warm": the gauge says the battery is overheating, or the panel sensor reads WarmC or more. Cleared only at
// ClearC or below with the gauge back to normal, so a reading near the limit does not make the warning flicker.
// Xochitl's own limit (its tempindicator "warningDegrees") is set in configuration this tablet does not expose as a
// file; WarmC is Inkie's choice, not a recovered stock value.
struct HeatWatch {static constexpr double WarmC=40,ClearC=37;bool hot{};
 bool update(const Temperature& t)noexcept{const bool before=hot;if(t.batteryHot||(t.panel&&*t.panel>=WarmC))hot=true;else if(!t.panel||*t.panel<=ClearC)hot=false;return hot!=before;}};
// The way the tablet is held, from its accelerometer (lis2dw12_accel on the iio bus). The raw axes go through the
// mount matrix into the device frame (IIO: x toward the right edge, y toward the top edge, z out of the screen); at rest
// the reading points away from the ground. Clockwise: the top edge turned to the right; anticlockwise: to the left.
enum class Turn:unsigned {Portrait,Clockwise,Anticlockwise};
struct Accel {double x{},y{},z{};};
inline std::optional<std::filesystem::path> accelerometer(const std::filesystem::path& iio="/sys/bus/iio/devices")noexcept{
 try{for(const auto& e:std::filesystem::directory_iterator(iio)){const auto name=detail::line(e.path()/"name");if(name&&*name=="lis2dw12_accel")return e.path();}}catch(...){}
 return std::nullopt;
}
inline std::optional<Accel> readAccel(const std::filesystem::path& device)noexcept{
 auto number=[](const std::string& v)->std::optional<double>{try{std::size_t n=0;const double x=std::stod(v,&n);if(n!=v.size()||!std::isfinite(x)||std::abs(x)>1e6)return std::nullopt;return x;}catch(...){return std::nullopt;}};
 std::array<double,3> raw{};for(int i=0;i<3;++i){auto v=detail::line(device/(std::string("in_accel_")+char('x'+i)+"_raw"));if(!v)return std::nullopt;auto x=number(*v);if(!x)return std::nullopt;raw[std::size_t(i)]=*x;}
 // Mount matrix "a, b, c; d, e, f; g, h, i": rows give device x, y, z from sensor x, y, z. Identity when absent or malformed.
 std::array<double,9> m{1,0,0,0,1,0,0,0,1};
 if(auto v=detail::line(device/"in_accel_mount_matrix")){std::array<double,9> parsed{};std::size_t at=0,count=0;std::string s=*v;for(auto& ch:s)if(ch==';')ch=',';bool ok=true;
  while(ok&&at<=s.size()&&count<10){const auto end=std::min(s.find(',',at),s.size());std::string part=s.substr(at,end-at);while(!part.empty()&&std::isspace(static_cast<unsigned char>(part.front())))part.erase(part.begin());while(!part.empty()&&std::isspace(static_cast<unsigned char>(part.back())))part.pop_back();auto x=number(part);if(!x||count>=9)ok=false;else parsed[count++]=*x;at=end+1;}
  if(ok&&count==9)m=parsed;}
 return Accel{m[0]*raw[0]+m[1]*raw[1]+m[2]*raw[2],m[3]*raw[0]+m[4]*raw[1]+m[5]*raw[2],m[6]*raw[0]+m[7]*raw[1]+m[8]*raw[2]};
}
// Reads the accelerometer away from the caller's thread. On the Paper Pro one reading of the three raw axes takes about
// 0.75 s (journal 2026-10-19, build fbb431d: with Auto-rotate on, every shell tick took 755 ms, so every tap waited
// that long). The caller takes the newest reading without waiting; the reader thread takes one reading per Period at
// most, and idles while paused. Each reading is taken once.
class AccelReader {
public:
 using Read=std::function<std::optional<Accel>()>;
 static constexpr std::chrono::milliseconds Period{300};
 AccelReader()=default;AccelReader(const AccelReader&)=delete;AccelReader& operator=(const AccelReader&)=delete;
 ~AccelReader(){{std::lock_guard<std::mutex> l(m_);stop_=true;}wake_.notify_all();if(thread_.joinable())thread_.join();}
 // Read with r (on the reader thread) until paused; the thread starts on first use. Never waits for a reading.
 void run(Read r)noexcept{{std::lock_guard<std::mutex> l(m_);read_=std::move(r);on_=true;}if(!thread_.joinable()){try{thread_=std::thread([this]{loop();});}catch(...){std::lock_guard<std::mutex> l(m_);on_=false;}}wake_.notify_all();}
 void pause()noexcept{{std::lock_guard<std::mutex> l(m_);on_=false;fresh_=false;}wake_.notify_all();}
 std::optional<Accel> take()noexcept{std::lock_guard<std::mutex> l(m_);if(!fresh_)return std::nullopt;fresh_=false;return latest_;}
 std::chrono::milliseconds lastRead()const noexcept{std::lock_guard<std::mutex> l(m_);return took_;}
private:
 void loop()noexcept{std::unique_lock<std::mutex> l(m_);while(!stop_){if(!on_||!read_){wake_.wait(l,[&]{return stop_||(on_&&read_);});continue;}
   auto read=read_;const auto start=std::chrono::steady_clock::now();l.unlock();std::optional<Accel> a;try{a=read();}catch(...){}l.lock();
   took_=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start);if(a&&on_){latest_=*a;fresh_=true;}
   wake_.wait_until(l,start+Period,[&]{return stop_||!on_;});}}
 mutable std::mutex m_;std::condition_variable wake_;Read read_;bool on_{},stop_{},fresh_{};Accel latest_{};std::chrono::milliseconds took_{};std::thread thread_;
};
// Settles the way the tablet is held. A new orientation needs the tilt within 30 degrees of it for Polls readings in a
// row, so a tablet held at an angle never flips; lying flat (the screen's plane carries under half of gravity) or upside
// down keeps the last orientation. angle: 0 upright, -90 top edge to the right, +90 top edge to the left.
struct OrientationWatch {static constexpr int Polls=3;static constexpr double Margin=30;Turn turn{Turn::Portrait};
 static double angle(const Accel& a)noexcept{return std::atan2(a.x,a.y)*180/std::numbers::pi;}
 static std::optional<Turn> classify(const Accel& a)noexcept{const double plane=std::hypot(a.x,a.y),all=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);if(!(all>0)||plane<.5*all)return std::nullopt;const double d=angle(a);
  if(std::abs(d)<=Margin)return Turn::Portrait;if(std::abs(d+90)<=Margin)return Turn::Clockwise;if(std::abs(d-90)<=Margin)return Turn::Anticlockwise;return std::nullopt;}
 bool update(const Accel& a)noexcept{const auto c=classify(a);if(!c||*c==turn){count_=0;return false;}if(*c!=candidate_||!count_){candidate_=*c;count_=1;}else ++count_;if(count_<Polls)return false;turn=*c;count_=0;return true;}
private:Turn candidate_{Turn::Portrait};int count_{};
};
// Wi-Fi radio link: true when wlan0 is up (associated), false when down or dormant, nothing when unknown.
inline std::optional<bool> wifiLink(const std::filesystem::path& root="/sys/class/net")noexcept{
 if(auto v=detail::line(root/"wlan0"/"operstate")){if(*v=="up")return true;if(*v=="down"||*v=="dormant"||*v=="lowerlayerdown")return false;}
 return std::nullopt;
}
// Storage of the filesystem holding a path (the library root lives on /home): total and available to this user.
struct Storage {std::int64_t total{-1},available{-1};bool operator==(const Storage&)const=default;};
inline Storage readStorage(const std::filesystem::path& path)noexcept{
 struct statvfs s{};if(path.empty()||::statvfs(path.c_str(),&s)!=0||s.f_frsize==0||s.f_blocks==0)return {};
 return {std::int64_t(s.f_blocks)*std::int64_t(s.f_frsize),std::int64_t(s.f_bavail)*std::int64_t(s.f_frsize)};
}
// The tablet's software version: IMG_VERSION from os-release (3.27.3.0 on the inventoried tablet); empty if absent
// or not a plain dotted version.
inline std::string firmware(const std::filesystem::path& osRelease="/etc/os-release")noexcept{
 try{std::ifstream in(osRelease);std::string line;int lines=0;
  while(++lines<=64&&std::getline(in,line)){if(line.rfind("IMG_VERSION=",0)!=0)continue;std::string v=line.substr(12);if(v.size()>=2&&v.front()=='"'&&v.back()=='"')v=v.substr(1,v.size()-2);
   if(v.empty()||v.size()>32)return {};for(char ch:v)if(!std::isdigit(static_cast<unsigned char>(ch))&&ch!='.')return {};return v;}}catch(...){}
 return {};
}
}
