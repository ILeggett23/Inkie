// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Device status bridge (read-only). Battery state comes from the Paper Pro's fuel gauge in sysfs; the same files
// the E2 device runs read on this tablet (max1726x_battery/status and capacity, power_supply/*/online). Nothing is
// written and nothing is guessed: an unreadable or malformed value is reported as unknown, and the UI then shows
// no battery at all rather than a made-up number. The Wi-Fi link is the kernel's wlan0 operstate (inventory of
// 2026-10-05, firmware 3.27.3.0): "up" once associated. See docs/system-settings-integration.md.
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <cstdint>
#include <sys/statvfs.h>
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
