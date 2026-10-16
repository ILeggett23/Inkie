// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
namespace spike {
enum class MaintenanceLock {Acquired,Busy,Failed};
enum class MaintenanceResult {Submitted,Deferred,Failed};
struct MaintenanceCalls {
 virtual void prepare()noexcept=0;
 virtual MaintenanceLock tryBegin()noexcept=0;
 virtual void request()noexcept=0;
 virtual void publish()noexcept=0; // normal return releases the admitted queue lock
 virtual void retry()noexcept=0;   // preserve the vendor timer's own interval
 virtual ~MaintenanceCalls()=default;
};
inline MaintenanceResult runMaintenance(MaintenanceCalls& calls,bool foregroundPump)noexcept{
 if(!foregroundPump){calls.retry();return MaintenanceResult::Deferred;}
 calls.prepare();
 switch(calls.tryBegin()){
 case MaintenanceLock::Busy:calls.retry();return MaintenanceResult::Deferred;
 case MaintenanceLock::Failed:return MaintenanceResult::Failed;
 case MaintenanceLock::Acquired:calls.request();calls.publish();return MaintenanceResult::Submitted;
 }
 return MaintenanceResult::Failed;
}
}
