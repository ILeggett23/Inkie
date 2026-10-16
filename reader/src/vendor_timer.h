// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "vendor_maintenance.h"
#include <QObject>
#include <QTimer>
#include <QTimerEvent>
#include <QThread>
#include <QPointer>
#include <cstdlib>
namespace spike {
// Process-local filter on the one pinned, owned maintenance timer. Other timers
// and the vendor's scheduling/temperature decisions remain untouched.
class MaintenanceTimer final:public QObject {
 QPointer<QTimer> timer_;MaintenanceCalls& calls_;const bool& pumping_;
 bool eventFilter(QObject* watched,QEvent* event)override{
  if(watched!=timer_.data()||event->type()!=QEvent::Timer||static_cast<QTimerEvent*>(event)->timerId()!=timer_->timerId())return false;
  timer_->stop();
  if(runMaintenance(calls_,pumping_)==MaintenanceResult::Failed)std::abort();
  return true;
 }
public:
 MaintenanceTimer(QTimer& timer,MaintenanceCalls& calls,const bool& pumping):timer_(&timer),calls_(calls),pumping_(pumping){timer_->installEventFilter(this);}
 ~MaintenanceTimer()override{if(timer_)timer_->removeEventFilter(this);}
 static bool compatible(const QTimer& timer)noexcept{return timer.metaObject()->inherits(&QTimer::staticMetaObject)&&timer.thread()==QThread::currentThread()&&timer.isSingleShot()&&timer.interval()>0;}
};
}
