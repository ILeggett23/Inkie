// SPDX-License-Identifier: AGPL-3.0-only
#include "wifi_control.h"
#include <QList>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <map>
#include <thread>
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace wifi {
namespace {
QList<QByteArray> lines(const QByteArray& reply){QList<QByteArray> out;for(auto line:reply.split('\n'))if(!line.isEmpty())out.push_back(line);return out;}
bool ok(const std::optional<QByteArray>& reply){return reply&&reply->trimmed()=="OK";}
}
Security security(const QByteArray& flags){
 if(flags.contains("EAP"))return Security::Enterprise;if(flags.contains("[WEP"))return Security::Wep;
 const bool psk=flags.contains("PSK"),sae=flags.contains("SAE");
 return psk&&sae?Security::PskSae:sae?Security::Sae:psk?Security::Psk:Security::Open;
}
QString ssidText(const QByteArray& in){
 QByteArray out;
 for(int i=0;i<in.size();++i){
  if(in[i]!='\\'||i+1>=in.size()){out+=in[i];continue;}
  const char c=in[++i];
  if(c=='x'&&i+2<in.size()){bool good=false;const int v=in.mid(i+1,2).toInt(&good,16);if(good){out+=char(v);i+=2;continue;}}
  out+=c=='n'?'\n':c=='r'?'\r':c=='t'?'\t':c=='e'?'\x1b':c;
 }
 return QString::fromUtf8(out);
}
QByteArray ssidHex(const QString& ssid){return ssid.toUtf8().toHex();}
Status status(const QByteArray& reply){
 Status s;s.available=true;
 for(const auto& line:lines(reply)){const int eq=line.indexOf('=');if(eq<=0)continue;const auto key=line.left(eq),value=line.mid(eq+1);
  if(key=="wpa_state")s.state=QString::fromLatin1(value);else if(key=="ssid")s.ssid=ssidText(value);else if(key=="ip_address")s.ip=QString::fromLatin1(value);}
 return s;
}
std::vector<Network> networks(const QByteArray& scanResults,const QByteArray& listNetworks,const QString& currentSsid){
 std::map<QString,Network> byName;
 auto rows=lines(scanResults);
 for(int n=1;n<rows.size();++n){auto f=rows[n].split('\t');if(f.size()<5)continue;const auto name=ssidText(f[4]);if(name.isEmpty()||name.contains(QChar(0)))continue;
  bool good=false;const int level=f[2].toInt(&good);if(!good)continue;auto& net=byName[name];
  if(net.ssid.isEmpty()||level>net.signal){net.ssid=name;net.signal=level;net.security=security(f[3]);}}
 auto saved=lines(listNetworks);
 for(int n=1;n<saved.size();++n){auto f=saved[n].split('\t');if(f.size()<2)continue;bool good=false;const int id=f[0].toInt(&good);if(!good)continue;const auto name=ssidText(f[1]);
  auto at=byName.find(name);const bool current=f.size()>=4&&f[3].contains("[CURRENT]");
  if(at!=byName.end()){at->second.savedId=id;at->second.current=at->second.current||current;}
  else if(current){Network net;net.ssid=name;net.savedId=id;net.current=true;byName[name]=net;}}
 std::vector<Network> out;for(auto& [name,net]:byName){if(!currentSsid.isEmpty()&&name==currentSsid)net.current=true;out.push_back(net);}
 std::stable_sort(out.begin(),out.end(),[](const Network& a,const Network& b){if(a.current!=b.current)return a.current;if((a.savedId>=0)!=(b.savedId>=0))return a.savedId>=0;return a.signal>b.signal;});
 return out;
}
std::optional<QByteArray> Control::request(const QByteArray& command,int timeoutMs,const std::atomic<bool>* stop)const{
 if(stop&&stop->load())return std::nullopt;
 const int fd=::socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);if(fd<0)return std::nullopt;
 struct Close{int fd;~Close(){::close(fd);}} closer{fd};
 // Abstract client address: valid across the reader unit's private /tmp, removed with the socket.
 static std::atomic<unsigned> counter{0};const auto name="inkbound-wifi-"+std::to_string(getpid())+"-"+std::to_string(++counter);
 sockaddr_un local{};local.sun_family=AF_UNIX;if(name.size()+1>=sizeof local.sun_path)return std::nullopt;std::memcpy(local.sun_path+1,name.data(),name.size());
 if(::bind(fd,reinterpret_cast<sockaddr*>(&local),socklen_t(offsetof(sockaddr_un,sun_path)+1+name.size())))return std::nullopt;
 sockaddr_un remote{};remote.sun_family=AF_UNIX;if(path_.empty()||path_.size()>=sizeof remote.sun_path)return std::nullopt;std::memcpy(remote.sun_path,path_.data(),path_.size());
 if(::connect(fd,reinterpret_cast<sockaddr*>(&remote),sizeof remote))return std::nullopt;
 if(::send(fd,command.constData(),std::size_t(command.size()),0)!=command.size())return std::nullopt;
 const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);char buffer[16384];
 for(;;){
  const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now()).count();if(left<=0||(stop&&stop->load()))return std::nullopt;
  pollfd p{fd,POLLIN,0};const int r=::poll(&p,1,stop?int(std::min<long long>(left,100)):int(left));if(r<0&&errno==EINTR)continue;if(r<0)return std::nullopt;if(r==0){if(stop)continue;return std::nullopt;}
  const ssize_t n=::recv(fd,buffer,sizeof buffer,0);if(n<0){if(errno==EINTR)continue;return std::nullopt;}
  if(n>0&&buffer[0]=='<')continue; /* an unsolicited event, not our reply */
  return QByteArray(buffer,int(n));
 }
}
Status Control::status(const std::atomic<bool>* stop)const{auto reply=request("STATUS",3000,stop);if(!reply)return {};return wifi::status(*reply);}
bool Control::reconnect(const Status& s,const std::atomic<bool>* stop)const{return s.available&&(s.state=="DISCONNECTED"||s.state=="INACTIVE")&&ok(request("REASSOCIATE",3000,stop));}
bool Control::scan(const std::atomic<bool>* stop)const{auto reply=request("SCAN",3000,stop);return reply&&(reply->trimmed()=="OK"||reply->startsWith("FAIL-BUSY"));}
std::vector<Network> Control::networks(const std::atomic<bool>* stop)const{
 auto results=request("SCAN_RESULTS",3000,stop),saved=request("LIST_NETWORKS",3000,stop);if(!results||!saved)return {};
 return wifi::networks(*results,*saved,status(stop).ssid);
}
QString Control::connect(const Network& net,const QString& password,int timeoutMs,const std::atomic<bool>* stop)const{
 auto command=[&](const QByteArray& command){return request(command,3000,stop);};
 if(!command("PING"))return "WI-FI IS OFF OR UNAVAILABLE";
 const bool reuse=net.savedId>=0&&password.isEmpty();int id=net.savedId;
 if(!reuse){
  if(net.security==Security::Wep||net.security==Security::Enterprise)return "THIS NETWORK TYPE IS NOT SUPPORTED HERE";
  if(net.security!=Security::Open){if(password.size()<8||password.size()>63)return "PASSWORD MUST BE 8 TO 63 CHARACTERS";for(auto c:password)if(c.unicode()<32)return "PASSWORD HAS AN UNSUPPORTED CHARACTER";}
  auto added=command("ADD_NETWORK");bool good=false;id=added?added->trimmed().toInt(&good):-1;if(!good||id<0)return "WI-FI SERVICE REFUSED THE NETWORK";
  // Same shape as the stock app's entries (priority, id_str numbering) so the stock UI lists it too.
  int nextIdStr=1;auto saved=command("LIST_NETWORKS");if(saved){auto rows=lines(*saved);for(int n=1;n<rows.size();++n){bool g=false;const int other=rows[n].split('\t')[0].toInt(&g);if(!g||other==id)continue;auto v=command("GET_NETWORK "+QByteArray::number(other)+" id_str");if(v){bool num=false;const int value=QByteArray(*v).replace("\"","").trimmed().toInt(&num);if(num)nextIdStr=std::max(nextIdStr,value+1);}}}
  const auto quoted="\""+QByteArray(password.toUtf8()).replace("\\","\\\\").replace("\"","\\\"")+"\"";const QByteArray prefix="SET_NETWORK "+QByteArray::number(id)+" ";
  QList<QByteArray> settings{"ssid "+ssidHex(net.ssid),"priority 1","id_str \""+QByteArray::number(nextIdStr)+"\""};
  switch(net.security){
   case Security::Open:settings<<"key_mgmt NONE";break;
   case Security::Psk:settings<<"key_mgmt WPA-PSK WPA-PSK-SHA256"<<"psk "+quoted<<"ieee80211w 1";break;
   case Security::Sae:settings<<"key_mgmt SAE"<<"sae_password "+quoted<<"ieee80211w 2";break;
   default:settings<<"key_mgmt WPA-PSK WPA-PSK-SHA256 SAE"<<"psk "+quoted<<"sae_password "+quoted<<"ieee80211w 1";break;
  }
  for(const auto& s:settings)if(!ok(command(prefix+s))){request("REMOVE_NETWORK "+QByteArray::number(id),500);return "WI-FI SERVICE REFUSED THE SETTINGS";}
 }
 if(!ok(command("SELECT_NETWORK "+QByteArray::number(id)))){if(!reuse)request("REMOVE_NETWORK "+QByteArray::number(id),500);request("ENABLE_NETWORK all",500);return "WI-FI SERVICE REFUSED THE NETWORK";}
 bool connected=false;
 for(const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);std::chrono::steady_clock::now()<end&&!(stop&&stop->load());){
  auto s=status(stop);if(s.connected()&&s.ssid==net.ssid){connected=true;break;}
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
 }
 // SELECT_NETWORK disables every other network; re-enable them in all cases.
 request("ENABLE_NETWORK all",500);
 if(!connected){if(!reuse)request("REMOVE_NETWORK "+QByteArray::number(id),500);return "COULD NOT CONNECT - CHECK THE PASSWORD";}
 if(!reuse)command("SAVE_CONFIG");
 return {};
}
}
