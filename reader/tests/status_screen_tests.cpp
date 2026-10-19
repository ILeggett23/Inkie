#include "ib/status_screen.h"
#include "device_status.h"
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>
int main(int argc,char**argv){try{if(argc!=2||!ib::text::load(std::filesystem::path(argv[1])/"assets/fonts"))return 2;using namespace ib;namespace s=status;unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("status screen regression");};int last=0;for(int n=1;n<=s::LitLevels;++n){const int value=s::nativeLevel(n,2047);check(value>last&&value<=2047&&s::nearestStep(value,2047)==n);last=value;}check(last==2047&&s::nativeLevel(0,2047)==0&&s::nativeLevel(6,2047)==-1&&s::nativeLevel(1,4)==-1&&s::nearestStep(2048,2047)==-1);
 std::vector<std::uint32_t> pixels(1632*2160,0xffffffffu);Canvas canvas({pixels,1620,2160,1632});Targets targets;s::Model model;model.clock="23:59";model.zone="UTC";s::bar(canvas,targets,model,s::ReaderBar);check(targets.rect(s::Open).x1==1496&&targets.find(1250,50)==s::Open);check(std::any_of(pixels.begin(),pixels.end(),[](auto p){return p!=0xffffffffu;}));check(s::ReaderBar.x1<=1620-112);
 // Temperature: the readings by hwmon name and the gauge, the warm/clear edges, and the bar's red bulb only while warm.
 {const auto root=std::filesystem::temp_directory_path()/("inkie-temp-"+std::to_string(::getpid()));std::filesystem::remove_all(root);auto put=[&](const std::filesystem::path& f,const std::string& v){std::filesystem::create_directories(f.parent_path());std::ofstream(f)<<v<<"\n";};
  put(root/"hwmon/hwmon1/name","cpu_thermal");put(root/"hwmon/hwmon1/temp1_input","36000");put(root/"hwmon/hwmon9/name","rm_hwmon_temp");put(root/"hwmon/hwmon9/temp1_input","24800");put(root/"ps/max1726x_battery/temp","230");put(root/"ps/max1726x_battery/health","Good");
  auto t=device::readTemperature(root/"hwmon",root/"ps");check(t.panel&&*t.panel==24.8&&t.cpu&&*t.cpu==36.0&&t.battery&&*t.battery==23.0&&!t.batteryHot);device::HeatWatch w;check(!w.update(t)&&!w.hot);
  put(root/"hwmon/hwmon9/temp1_input","40100");t=device::readTemperature(root/"hwmon",root/"ps");check(w.update(t)&&w.hot);put(root/"hwmon/hwmon9/temp1_input","38500");check(!w.update(device::readTemperature(root/"hwmon",root/"ps"))&&w.hot); /* inside the gap: still warm */
  put(root/"hwmon/hwmon9/temp1_input","36900");check(w.update(device::readTemperature(root/"hwmon",root/"ps"))&&!w.hot);put(root/"ps/max1726x_battery/health","Overheat");check(w.update(device::readTemperature(root/"hwmon",root/"ps"))&&w.hot); /* the gauge alone */
  put(root/"hwmon/hwmon9/temp1_input","garbage");check(!device::readTemperature(root/"hwmon",root/"ps").panel);
  // Orientation: the accelerometer found by name, its raw axes through the device's mount matrix (sensor x and y swap,
  // z flips), and the watch: three settled polls to turn, a 30 degree margin, flat or upside down keeps the last turn.
  put(root/"iio/iio:device2/name","lis2dw12_tap");put(root/"iio/iio:device0/name","lis2dw12_accel");const auto acc=root/"iio/iio:device0";put(acc/"in_accel_mount_matrix","0, 1, 0; 1, 0, 0; 0, 0, -1");
  auto axes=[&](int x,int y,int z){put(acc/"in_accel_x_raw",std::to_string(x));put(acc/"in_accel_y_raw",std::to_string(y));put(acc/"in_accel_z_raw",std::to_string(z));return device::readAccel(acc);};
  check(device::accelerometer(root/"iio")==acc&&!device::accelerometer(root/"none"));auto a=axes(0,1000,0);check(a&&a->x==1000&&a->y==0&&a->z==0); /* sensor y is device x */
  device::OrientationWatch o;using T=device::Turn;auto feed=[&](int dx,int dy,int dz){return o.update(device::Accel{double(dx),double(dy),double(dz)});};
  check(!feed(0,1000,0)&&o.turn==T::Portrait);check(!feed(-1000,0,0)&&!feed(-1000,0,0)&&o.turn==T::Portrait&&feed(-1000,0,0)&&o.turn==T::Clockwise); /* top edge to the right */
  check(!feed(0,0,1000)&&!feed(0,0,1000)&&!feed(0,0,1000)&&o.turn==T::Clockwise); /* flat on a table: kept */
  check(!feed(-707,707,0)&&!feed(-707,707,0)&&!feed(-707,707,0)&&o.turn==T::Clockwise); /* 45 degrees: inside the margin, kept */
  check(!feed(0,-1000,0)&&!feed(0,-1000,0)&&!feed(0,-1000,0)&&o.turn==T::Clockwise); /* upside down: kept */
  check(!feed(1000,0,0)&&!feed(1000,0,0)&&!feed(0,1000,0)&&o.turn==T::Clockwise); /* interrupted: no turn */
  check(!feed(1000,0,0)&&!feed(1000,0,0)&&feed(1000,0,0)&&o.turn==T::Anticlockwise&&std::lround(device::OrientationWatch::angle({1000,0,0}))==90);
  check(!feed(-200,1000,0)&&!feed(-200,1000,0)&&feed(-200,1000,0)&&o.turn==T::Portrait); /* 11 degrees off upright */
  // The reader thread: the caller never waits for a slow sensor (0.75 s a reading on the Paper Pro), each reading is
  // taken once, and a paused reader reads nothing more (at most the reading already under way, then discarded).
  {device::AccelReader r;std::atomic<int> reads{0};check(!r.take());r.run([&]{++reads;std::this_thread::sleep_for(std::chrono::milliseconds(400));return std::optional<device::Accel>(device::Accel{1,2,3});});
   const auto t0=std::chrono::steady_clock::now();check(!r.take());r.run([&]{++reads;std::this_thread::sleep_for(std::chrono::milliseconds(400));return std::optional<device::Accel>(device::Accel{1,2,3});});check(std::chrono::steady_clock::now()-t0<std::chrono::milliseconds(100));
   std::optional<device::Accel> got;for(int i=0;i<500&&!got;++i){got=r.take();if(!got)std::this_thread::sleep_for(std::chrono::milliseconds(10));}check(got&&got->x==1&&got->y==2&&got->z==3&&r.lastRead()>=std::chrono::milliseconds(400));check(!r.take());
   r.pause();const int paused=reads.load();std::this_thread::sleep_for(std::chrono::milliseconds(1000));check(reads.load()<=paused+1&&!r.take());
   axes(0,1000,0);r.run([acc]{return device::readAccel(acc);});got={};for(int i=0;i<500&&!got;++i){got=r.take();if(!got)std::this_thread::sleep_for(std::chrono::milliseconds(10));}check(got&&got->x==1000&&got->y==0);}
  put(acc/"in_accel_mount_matrix","garbage");a=axes(1,2,3);check(a&&a->x==1&&a->y==2&&a->z==3); /* identity when malformed */put(acc/"in_accel_z_raw","x");check(!device::readAccel(acc));
  std::filesystem::remove_all(root);
  std::vector<std::uint32_t> px(1632*2160,0xffffffffu);Canvas cv({px,1620,2160,1632});Targets tg;s::Model cool;cool.clock="12:00";cool.clockOnly=true;s::bar(cv,tg,cool,s::ReaderClock);auto red=[&]{for(int y=s::ReaderClock.y0;y<s::ReaderClock.y1;++y)for(int x=s::ReaderClock.x0;x<s::ReaderClock.x1;++x)if(px[std::size_t(y)*1632+x]==theme::Caution)return true;return false;};check(!red());
  auto warm=cool;warm.hot=true;warm.degrees=41;s::bar(cv,tg,warm,s::ReaderClock);check(red());}
 model.battery=73;model.wifi=0;model.charging=true;model.light=819;model.maximum=2047;s::panel(canvas,targets,model);check(targets.find(1250,50)==s::Open);for(int n=0;n<=5;++n){const auto r=targets.rect(n?s::LevelBase+n-1:s::LightOff);check(r.x1-r.x0>=96&&r.y1-r.y0>=96&&r.x0>=s::Panel.x0&&r.x1<=s::Panel.x1);check(targets.find((r.x0+r.x1)/2,(r.y0+r.y1)/2)==(n?s::LevelBase+n-1:s::LightOff));}check(!targets.rect(s::Wifi).empty()&&!targets.rect(s::Battery).empty());check(targets.rect(s::Rotate).empty()); /* no sensor: shown, not a target */{auto with=model;with.rotate=1;Targets rt;s::panel(canvas,rt,with);const auto r=rt.rect(s::Rotate);check(!r.empty()&&r.y0>=rt.rect(s::Wifi).y1&&r.y1<=rt.rect(s::LightOff).y0&&r.x1<=s::Panel.x1);}std::fill(pixels.begin(),pixels.end(),0xffffffffu);targets.clear();model.clockOnly=true;s::bar(canvas,targets,model,s::ReaderClock);const auto clockOnly=pixels;check(targets.rect(s::Open).x0==1280&&targets.find(1200,50)==-1);model.battery=3;model.wifi=1;model.charging=false;std::fill(pixels.begin(),pixels.end(),0xffffffffu);targets.clear();s::bar(canvas,targets,model,s::ReaderClock);check(pixels==clockOnly);check(std::all_of(pixels.begin()+8*1632+1080,pixels.begin()+8*1632+1280,[](auto p){return p==0xffffffffu;}));std::printf("STATUS_SCREEN_PASS checks=%u lit_levels=5 off=separate vendor_calls=0 input_opens=0\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"STATUS_SCREEN_FAIL %s\n",e.what());return 1;}}
