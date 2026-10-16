// SPDX-License-Identifier: AGPL-3.0-only
#include "vendor_timer.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <cstdio>
#include <memory>
#include <stdexcept>
namespace {unsigned checks{};void check(bool x,const char* why){++checks;if(!x)throw std::runtime_error(why);}
struct Calls final:spike::MaintenanceCalls {QTimer* timer;unsigned tries{},requests{},ends{},retries{};unsigned busy{2};explicit Calls(QTimer& t):timer(&t){}
 void prepare()noexcept override{}spike::MaintenanceLock tryBegin()noexcept override{return ++tries<=busy?spike::MaintenanceLock::Busy:spike::MaintenanceLock::Acquired;}
 void request()noexcept override{++requests;}void publish()noexcept override{++ends;}void retry()noexcept override{++retries;timer->start();}
};}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 QTimer timer;timer.setSingleShot(true);timer.setInterval(2);check(spike::MaintenanceTimer::compatible(timer),"owned timer compatibility");bool pumping=false;Calls calls(timer);spike::MaintenanceTimer filter(timer,calls,pumping);unsigned original{},other{};QObject::connect(&timer,&QTimer::timeout,[&]{++original;});QTimer unrelated;unrelated.setSingleShot(true);QObject::connect(&unrelated,&QTimer::timeout,[&]{++other;});
 QEventLoop loop;QTimer poll;poll.setInterval(1);QObject::connect(&poll,&QTimer::timeout,[&]{if(calls.retries>0)pumping=true;if(calls.requests)loop.quit();});QTimer deadline;deadline.setSingleShot(true);QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
 timer.start();unrelated.start(3);poll.start();deadline.start(2000);loop.exec();
 check(calls.requests==1&&calls.ends==1&&calls.retries>=3,"deferred timer debt eventually executes once");check(original==0,"blocking original maintenance slot is not invoked");check(other==1,"unrelated timers remain operational");check(timer.interval()==2&&timer.isSingleShot()&&!timer.isActive(),"original interval and single-shot completion preserved");
 const auto tries=calls.tries;QTimerEvent wrong(999999);QCoreApplication::sendEvent(&timer,&wrong);check(calls.tries==tries,"unrelated timer ID ignored");
 {auto owned=std::make_unique<QTimer>();owned->setInterval(1);owned->setSingleShot(true);Calls another(*owned);auto guard=std::make_unique<spike::MaintenanceTimer>(*owned,another,pumping);owned.reset();guard.reset();}
 std::printf("VENDOR_TIMER_PASS checks=%u real_Qt_timer=true vendor_execution=false optical=NOT_RUN\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"VENDOR_TIMER_FAIL %s\n",e.what());return 1;}}
