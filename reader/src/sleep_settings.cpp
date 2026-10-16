// SPDX-License-Identifier: AGPL-3.0-only
#include "sleep_settings.h"
#include "underlay.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
namespace power {
bool validIdle(int minutes)noexcept{return std::find(std::begin(IdleChoices),std::end(IdleChoices),minutes)!=std::end(IdleChoices);}
int stepIdle(int minutes,int delta)noexcept{
 const int n=int(std::size(IdleChoices));int i=0;while(i<n&&IdleChoices[i]!=minutes)++i;if(i==n)i=3; /* unknown values step from the default */
 return IdleChoices[std::clamp(i+(delta>0?1:delta<0?-1:0),0,n-1)];
}
std::filesystem::path settingsPath(const std::filesystem::path& data){return data/"settings"/"sleep.json";}
SleepSettings load(const std::filesystem::path& data)noexcept{
 SleepSettings s;
 try{
  const auto path=settingsPath(data);if(!std::filesystem::exists(path))return s;
  auto doc=QJsonDocument::fromJson(underlay::readBounded(path,4096));if(!doc.isObject())return s;auto o=doc.object();
  if(o["schema"].toInt()!=1)return s;
  if(o["cover"].isBool())s.cover=o["cover"].toBool();
  if(o["idleMinutes"].isDouble()&&validIdle(o["idleMinutes"].toInt()))s.idleMinutes=o["idleMinutes"].toInt();
 }catch(...){return SleepSettings{};}
 return s;
}
bool save(const std::filesystem::path& data,const SleepSettings& s)noexcept{
 try{
  if(!validIdle(s.idleMinutes))return false;
  underlay::privateDirectory(data/"settings");
  underlay::atomicFile(settingsPath(data),QJsonDocument(QJsonObject{{"schema",1},{"cover",s.cover},{"idleMinutes",s.idleMinutes}}).toJson(QJsonDocument::Compact));
  return true;
 }catch(...){return false;}
}
}
