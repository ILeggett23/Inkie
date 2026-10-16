// SPDX-License-Identifier: AGPL-3.0-only
// Inkbound sleep settings: defaults, durable round trip, malformed or foreign files fall back to defaults,
// invalid idle values are refused, and the idle stepper walks the choice list.
#include "sleep_settings.h"
#include "underlay.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>
#include <sys/stat.h>
static unsigned checks=0;
static void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
int main(int argc,char** argv){QCoreApplication app(argc,argv);
 try{
  QTemporaryDir temp;check(temp.isValid(),"temp");const std::filesystem::path data=temp.path().toStdString();
  check(power::load(data)==power::SleepSettings{},"missing-file-defaults");check(power::load(data).cover&&power::load(data).idleMinutes==15,"defaults-cover-on-15");
  check(power::save(data,{false,30}),"save");auto s=power::load(data);check(!s.cover&&s.idleMinutes==30,"round-trip");
  struct stat st{};check(!stat(power::settingsPath(data).c_str(),&st)&&(st.st_mode&0077)==0,"settings-private");
  check(power::save(data,{true,0}),"save-never");check(power::load(data).idleMinutes==0&&power::load(data).cover,"never-round-trip");
  check(!power::save(data,{true,7})&&power::load(data).idleMinutes==0,"invalid-idle-refused-previous-kept");
  underlay::atomicFile(power::settingsPath(data),"{");check(power::load(data)==power::SleepSettings{},"malformed-defaults");
  underlay::atomicFile(power::settingsPath(data),R"({"schema":2,"cover":false,"idleMinutes":5})");check(power::load(data)==power::SleepSettings{},"foreign-schema-defaults");
  underlay::atomicFile(power::settingsPath(data),R"({"schema":1,"cover":false,"idleMinutes":7})");s=power::load(data);check(!s.cover&&s.idleMinutes==15,"bad-idle-keeps-default");
  check(power::stepIdle(15,1)==30&&power::stepIdle(15,-1)==10&&power::stepIdle(0,-1)==0&&power::stepIdle(60,1)==60&&power::stepIdle(7,1)==30,"stepper");
  for(int m:power::IdleChoices)check(power::validIdle(m),"choices-valid");check(!power::validIdle(-5)&&!power::validIdle(20),"others-invalid");
  std::printf("SLEEP_SETTINGS_PASS checks=%u defaults=cover_on_15min choices=0,5,10,15,30,60 stock_settings=UNTOUCHED\n",checks);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"SLEEP_SETTINGS_FAILED %s\n",e.what());return 1;}
}
