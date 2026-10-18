// SPDX-License-Identifier: AGPL-3.0-only
// Library Wi-Fi: wpa_supplicant reply parsing, the exact control commands for connecting (against a
// fake control socket, never a real radio) and the screen's hits. A failed connect removes what it
// added and re-enables every other network; only a successful new network is saved.
#include "wifi_control.h"
#include <fstream>
#include <QTemporaryDir>
#include "device_status.h"
#include "wifi_view.h"
#include "ib/theme.h"
#include <QCoreApplication>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace {
unsigned checks{};
void check(bool b,const char* what){++checks;if(!b)throw std::runtime_error(what);}
const QByteArray Scan="bssid / frequency / signal level / flags / ssid\n"
 "aa:aa:aa:aa:aa:01\t5220\t-48\t[WPA2-PSK-CCMP][ESS]\tHome\n"
 "aa:aa:aa:aa:aa:02\t2412\t-71\t[WPA2-PSK-CCMP][ESS]\tHome\n"
 "aa:aa:aa:aa:aa:03\t2437\t-60\t[ESS]\tCafe Free\n"
 "aa:aa:aa:aa:aa:04\t2462\t-80\t[RSN-SAE-CCMP][ESS]\tNeighbour\\xe2\\x80\\x99s\n"
 "aa:aa:aa:aa:aa:05\t2462\t-50\t[WPA2-EAP-CCMP][ESS]\tOffice\n"
 "aa:aa:aa:aa:aa:06\t2462\t-50\t[ESS]\t\n";
const QByteArray Saved="network id / ssid / bssid / flags\n0\tHome\tany\t[CURRENT]\n1\tElsewhere\tany\t\n";
// A fake wpa_supplicant control socket: records every command and answers like the real one.
struct Fake {
 std::string path;int fd{-1};std::thread worker;std::atomic<bool> stop{false};std::vector<std::string> log;bool connects{true};bool selected{};
 explicit Fake(const std::string& p):path(p){
  fd=::socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);sockaddr_un a{};a.sun_family=AF_UNIX;std::strncpy(a.sun_path,path.c_str(),sizeof a.sun_path-1);::unlink(path.c_str());
  check(fd>=0&&::bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof a)==0,"fake socket");
  worker=std::thread([this]{char buffer[4096];while(!stop){pollfd p{fd,POLLIN,0};if(::poll(&p,1,50)<=0)continue;sockaddr_un from{};socklen_t len=sizeof from;
   const ssize_t n=::recvfrom(fd,buffer,sizeof buffer-1,0,reinterpret_cast<sockaddr*>(&from),&len);if(n<=0)continue;std::string cmd(buffer,std::size_t(n));log.push_back(cmd);
   std::string reply="OK\n";
   if(cmd=="NO_REPLY")continue;else if(cmd=="PING")reply="PONG\n";else if(cmd=="ADD_NETWORK")reply="2\n";else if(cmd=="SCAN_RESULTS")reply=Scan.toStdString();else if(cmd=="LIST_NETWORKS")reply=Saved.toStdString();
   else if(cmd.rfind("GET_NETWORK 0 id_str",0)==0)reply="\"6\"";else if(cmd.rfind("GET_NETWORK 1 id_str",0)==0)reply="\"7\"";
   else if(cmd.rfind("SELECT_NETWORK",0)==0)selected=true;
   else if(cmd=="STATUS")reply=selected&&connects?"wpa_state=COMPLETED\nssid=Cafe Free\nip_address=192.168.1.20\n":"wpa_state=SCANNING\n";
   ::sendto(fd,reply.data(),reply.size(),0,reinterpret_cast<sockaddr*>(&from),len);}});
 }
 ~Fake(){stop=true;worker.join();::close(fd);::unlink(path.c_str());}
 bool sent(const std::string& c)const{for(const auto& l:log)if(l==c)return true;return false;}
};
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);
 try{
  // Parsing.
  check(wifi::security("[WPA2-PSK-CCMP][ESS]")==wifi::Security::Psk&&wifi::security("[RSN-SAE-CCMP]")==wifi::Security::Sae&&wifi::security("[WPA2-PSK+SAE-CCMP]")==wifi::Security::PskSae,"security kinds");
  check(wifi::security("[ESS]")==wifi::Security::Open&&wifi::security("[WEP][ESS]")==wifi::Security::Wep&&wifi::security("[WPA2-EAP-CCMP]")==wifi::Security::Enterprise,"open, wep and enterprise");
  check(wifi::ssidText("Neighbour\\xe2\\x80\\x99s")==QString::fromUtf8("Neighbour’s")&&wifi::ssidText("a\\\\b\\\"c")=="a\\b\"c","escaped ssid");
  check(wifi::ssidHex("Home")=="486f6d65","hex ssid");
  auto list=wifi::networks(Scan,Saved,"Home");
  check(list.size()==4,"one entry per named network, hidden ones skipped");
  check(list[0].ssid=="Home"&&list[0].current&&list[0].savedId==0&&list[0].signal==-48,"current saved network first at its strongest signal");
  check(list[1].ssid=="Office"&&list[2].ssid=="Cafe Free"&&list[3].ssid==QString::fromUtf8("Neighbour’s"),"others by signal");
  check(!ux::supported(list[1])&&ux::supported(list[2]),"enterprise not offered");
  auto st=wifi::status("wpa_state=COMPLETED\nssid=Home\nip_address=10.0.0.4\n");check(st.connected()&&st.ssid=="Home"&&st.ip=="10.0.0.4","status");
  check(!wifi::status("wpa_state=COMPLETED\nssid=Home\n").connected(),"no address yet is not connected");
  check(!wifi::Control("/nonexistent/wpa").status().available,"no service is unavailable");
  // Connecting a new open network: added, selected, others re-enabled, saved for the stock app too.
  char dir[]="/tmp/inkbound-wifi-XXXXXX";check(mkdtemp(dir)!=nullptr,"temp dir");const std::string socket=std::string(dir)+"/wlan0";
  {Fake fake(socket);wifi::Control control(socket);auto nets=control.networks();check(nets.size()==4,"networks over the socket");
   wifi::Network cafe=nets[2];check(control.connect(cafe,{},3000).isEmpty(),"open network connects");
   check(fake.sent("ADD_NETWORK")&&fake.sent("SET_NETWORK 2 ssid "+wifi::ssidHex("Cafe Free").toStdString())&&fake.sent("SET_NETWORK 2 key_mgmt NONE"),"open network settings");
   check(fake.sent("SET_NETWORK 2 id_str \"8\"")&&fake.sent("SET_NETWORK 2 priority 1"),"stock-style id_str numbering");
   check(fake.sent("SELECT_NETWORK 2")&&fake.sent("ENABLE_NETWORK all")&&fake.sent("SAVE_CONFIG")&&!fake.sent("REMOVE_NETWORK 2"),"selected, re-enabled, saved");}
  // A password network that never connects: removed again, others re-enabled, nothing saved.
  {Fake fake(socket);fake.connects=false;wifi::Control control(socket);wifi::Network home;home.ssid="Neighbour";home.security=wifi::Security::Sae;
   check(control.connect(home,"short",1000)=="PASSWORD MUST BE 8 TO 63 CHARACTERS"&&!fake.sent("ADD_NETWORK"),"short password refused before any change");
   check(control.connect(home,"correct horse battery",1200)=="COULD NOT CONNECT - CHECK THE PASSWORD","failed connect reported");
   check(fake.sent("SET_NETWORK 2 key_mgmt SAE")&&fake.sent("SET_NETWORK 2 sae_password \"correct horse battery\"")&&fake.sent("SET_NETWORK 2 ieee80211w 2"),"WPA3 settings");
   check(fake.sent("REMOVE_NETWORK 2")&&fake.sent("ENABLE_NETWORK all")&&!fake.sent("SAVE_CONFIG"),"failed network removed, nothing saved");}
  // A saved network reconnects without adding or saving anything.
  {Fake fake(socket);wifi::Control control(socket);wifi::Network elsewhere;elsewhere.ssid="Cafe Free";elsewhere.savedId=1;
   check(control.connect(elsewhere,{},3000).isEmpty()&&fake.sent("SELECT_NETWORK 1")&&!fake.sent("ADD_NETWORK")&&!fake.sent("SAVE_CONFIG")&&fake.sent("ENABLE_NETWORK all"),"saved network reused");}
  {Fake fake(socket);wifi::Control control(socket);wifi::Status disconnected;disconnected.available=true;disconnected.state="DISCONNECTED";check(control.reconnect(disconnected)&&fake.sent("REASSOCIATE")&&!fake.sent("ENABLE_NETWORK all")&&!fake.sent("ADD_NETWORK"),"automatic reconnect uses only already enabled saved networks");for(const auto& state:{QString("COMPLETED"),QString("ASSOCIATING"),QString("SCANNING"),QString("INTERFACE_DISABLED")}){disconnected.state=state;check(!control.reconnect(disconnected),"automatic reconnect does not interrupt an active association or disabled radio");}}
  {Fake fake(socket);wifi::Control control(socket);std::atomic<bool> stop{false};const auto start=std::chrono::steady_clock::now();std::thread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(120));stop=true;});const auto reply=control.request("NO_REPLY",5000,&stop);cancel.join();check(!reply&&std::chrono::steady_clock::now()-start<std::chrono::seconds(1),"stalled automatic Wi-Fi requests cancel without blocking teardown");}
  rmdir(dir);
  // Screen.
  ux::WifiView view;check(view.displayedText().isEmpty()&&view.hit(300,500).action==ux::WifiView::Action::None,"inactive view");
  view.open();check(view.hit(300,500).action==ux::WifiView::Action::None&&view.hit(1400,100).action==ux::WifiView::Action::Close,"busy scan only allows close");
  view.update(list,st,{},false);
  check(view.hit(300,500).action==ux::WifiView::Action::None,"current network is not reselected");
  check(view.hit(300,680).action==ux::WifiView::Action::None,"enterprise row has no action");
  auto pick=view.hit(300,850);check(pick.action==ux::WifiView::Action::Select&&pick.index==2,"open network row selects");
  check(view.hit(300,1950).action==ux::WifiView::Action::Rescan,"rescan");
  auto text=view.displayedText().join('|');check(text.contains("CONNECTED TO Home")&&text.contains("NOT SUPPORTED HERE")&&text.contains("SHARED WITH THE REMARKABLE'S OWN WI-FI SETTINGS"),"screen text");
  ux::WifiView off;off.open();off.update({},{},{},false);check(off.displayedText().join('|').contains("WI-FI IS OFF"),"service off explained");
  // Painted signal: bars are solid up to the level and hollow above it (inactive bars once read as full), and a
  // network this tablet can't join shows none. Rows and bars are the view's own geometry (row n at 420+175n).
  {std::vector<wifi::Network> nets(4);const int dbm[]{-50,-60,-70,-90};for(int i=0;i<4;++i){nets[std::size_t(i)].ssid=QStringLiteral("Net %1").arg(i);nets[std::size_t(i)].signal=dbm[i];}
   nets[3].security=wifi::Security::Enterprise;nets.push_back(nets[0]);nets[4].ssid="Wep";nets[4].security=wifi::Security::Wep;
   ux::WifiView v;v.open();v.update(nets,{},{},false);std::vector<std::uint32_t> px(std::size_t(1632)*2160,0);v.paint({px,1620,2160,1632});
   auto solid=[&](int n,int b){const int y0=420+n*175,cx=1410+b*30,cy=y0+100-(b+1)*15/2;return px[std::size_t(cy)*1632+std::size_t(cx)]==ib::theme::InkPrimary;};
   auto inked=[&](int n){const int y0=420+n*175;for(int y=y0+30;y<y0+100;++y)for(int x=1400;x<1510;++x)if(px[std::size_t(y)*1632+std::size_t(x)]==ib::theme::InkPrimary)return true;return false;};
   const int levels[]{4,3,2};for(int n=0;n<3;++n)for(int b=0;b<4;++b)check(solid(n,b)==(b<levels[n]),"signal bar solid exactly up to the level");
   check(!inked(3)&&!inked(4),"networks this tablet can't join show no signal bars");}
  // Device status bridge (read-only): the tablet's fuel-gauge files in a fake sysfs tree; malformed or missing
  // values are unknown, never invented.
  {QTemporaryDir temp;check(temp.isValid(),"battery tree");const std::filesystem::path root=temp.path().toStdString();
   auto write=[&](const std::filesystem::path& f,const char* v){std::filesystem::create_directories(f.parent_path());std::ofstream(f)<<v;};
   check(!device::readBattery(root).percent&&device::readBattery(root).charge==device::Charge::Unknown,"no gauge: unknown battery");
   write(root/"max1726x_battery/capacity","82\n");write(root/"max1726x_battery/status","Charging\n");write(root/"usb/online","1\n");
   auto b=device::readBattery(root);check(b.percent==82&&b.charge==device::Charge::Charging&&b.charging()&&b.external,"gauge read");
   write(root/"max1726x_battery/capacity","101\n");write(root/"max1726x_battery/status","Discharging\n");write(root/"usb/online","0\n");
   b=device::readBattery(root);check(!b.percent&&b.charge==device::Charge::Discharging&&!b.charging()&&!b.external,"out-of-range capacity is unknown");
   write(root/"max1726x_battery/capacity","9x\n");check(!device::readBattery(root).percent,"malformed capacity is unknown");
   write(root/"max1726x_battery/capacity","100\n");write(root/"max1726x_battery/status","Full\n");b=device::readBattery(root);check(b.percent==100&&b.charge==device::Charge::Full&&b.charging(),"full counts as on charge");
   write(root/"max1726x_battery/status","Not charging\n");b=device::readBattery(root);check(b.charge==device::Charge::NotCharging&&!b.charging(),"not charging is not charging");
   write(root/"max1726x_battery/status","Overheat\n");check(device::readBattery(root).charge==device::Charge::Unknown,"unrecognised status is unknown");
   write(root/"nfc-marker-battery/online","1\n");write(root/"elants-marker-battery/online","1\n");check(!device::readBattery(root).external,"an attached Marker is not external power");
   write(root/"max77963-charger/online","1\n");check(device::readBattery(root).external,"the charger is external power");}
  {QTemporaryDir temp;check(temp.isValid(),"network tree");const std::filesystem::path root=temp.path().toStdString();
   auto write=[&](const std::filesystem::path& f,const char* v){std::filesystem::create_directories(f.parent_path());std::ofstream(f)<<v;};
   check(!device::wifiLink(root),"no wlan0: unknown link");write(root/"wlan0/operstate","up\n");check(device::wifiLink(root)==true,"wlan0 up is a link");
   write(root/"wlan0/operstate","down\n");check(device::wifiLink(root)==false,"wlan0 down is no link");write(root/"wlan0/operstate","dormant\n");check(device::wifiLink(root)==false,"dormant is no link");
   write(root/"wlan0/operstate","unknown\n");check(!device::wifiLink(root),"unknown operstate is unknown");}
  {QTemporaryDir temp;check(temp.isValid(),"storage tree");const std::filesystem::path root=temp.path().toStdString();
   auto write=[&](const std::filesystem::path& f,const char* v){std::filesystem::create_directories(f.parent_path());std::ofstream(f)<<v;};
   const auto storage=device::readStorage(root);check(storage.total>0&&storage.available>=0&&storage.available<=storage.total,"storage of a real filesystem");
   check(device::readStorage(root/"missing").total==-1&&device::readStorage({}).total==-1,"missing path: unknown storage");
   write(root/"os-release","NAME=\"reMarkable\"\nIMG_VERSION=\"3.27.3.0\"\n");check(device::firmware(root/"os-release")=="3.27.3.0","firmware version read");
   write(root/"os-release","IMG_VERSION=\"3.27; rm\"\n");check(device::firmware(root/"os-release").empty(),"a non-version firmware string is refused");
   check(device::firmware(root/"absent").empty(),"no os-release: no firmware");}

  {wifi::Network cafe;cafe.ssid="Cafe Free";cafe.savedId=1;wifi::Network home;home.ssid="Home";home.security=wifi::Security::Psk;
   wifi::Requests q;using D=wifi::Requests::Decision;
   check(q.submit({{},{},true})==D::Start&&q.busy(),"an automatic scan starts when idle");
   check(q.submit({cafe,{},false})==D::Queued&&q.queued(),"a tap on a network during an automatic scan waits for it");
   check(q.submit({home,"secret",false})==D::Queued,"a newer manual request replaces the waiting one");
   check(q.submit({{},{},true})==D::Refused,"automatic passes never wait");
   auto next=q.finish();check(next&&next->target&&next->target->ssid=="Home"&&next->password=="secret"&&!next->automatic&&!q.queued()&&!q.busy(),"the newest manual request runs next, with its own network and password");
   check(q.submit(*next)==D::Start&&q.submit({cafe,{},false})==D::Refused&&q.submit({{},{},false})==D::Refused,"a connection in progress refuses other requests");
   check(!q.finish()&&q.submit({{},{},false})==D::Start&&q.submit({cafe,{},false})==D::Queued,"a manual scan also lets a tap wait");}
  std::printf("WIFI_TESTS_PASS checks=%u radio=FAKE_CONTROL_SOCKET\n",checks);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"WIFI_TESTS_FAIL %s checks=%u\n",e.what(),checks);return 1;}
}
