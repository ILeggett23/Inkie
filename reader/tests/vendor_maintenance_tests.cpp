// SPDX-License-Identifier: AGPL-3.0-only
#include "vendor_maintenance.h"
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <pthread.h>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {unsigned checks{};void check(bool x,const char* why){++checks;if(!x)throw std::runtime_error(why);}
struct Calls final:spike::MaintenanceCalls {
 std::vector<char> order;spike::MaintenanceLock lock{spike::MaintenanceLock::Acquired};
 void prepare()noexcept override{order.push_back('p');}spike::MaintenanceLock tryBegin()noexcept override{order.push_back('l');return lock;}
 void request()noexcept override{order.push_back('r');}void publish()noexcept override{order.push_back('e');}void retry()noexcept override{order.push_back('d');}
};
struct Native final:spike::MaintenanceCalls {
 pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;unsigned submitted{},published{},deferred{};
 void prepare()noexcept override{}spike::MaintenanceLock tryBegin()noexcept override{const int n=pthread_mutex_trylock(&mutex);return n==0?spike::MaintenanceLock::Acquired:n==EBUSY?spike::MaintenanceLock::Busy:spike::MaintenanceLock::Failed;}
 void request()noexcept override{++submitted;}void publish()noexcept override{++published;if(pthread_mutex_unlock(&mutex))std::abort();}void retry()noexcept override{++deferred;}
 ~Native(){pthread_mutex_destroy(&mutex);}
};}
int main(){try{
 Calls c;check(spike::runMaintenance(c,false)==spike::MaintenanceResult::Deferred,"nested/non-pump event must defer");check(c.order==std::vector<char>{'d'},"no native work outside foreground pump");
 c.order.clear();c.lock=spike::MaintenanceLock::Busy;for(int n=0;n<3;++n)check(spike::runMaintenance(c,true)==spike::MaintenanceResult::Deferred,"busy must retain maintenance");check(c.order==std::vector<char>({'p','l','d','p','l','d','p','l','d'}),"busy must not request/publish or block");
 c.order.clear();c.lock=spike::MaintenanceLock::Acquired;check(spike::runMaintenance(c,true)==spike::MaintenanceResult::Submitted,"admitted maintenance");check(c.order==std::vector<char>({'p','l','r','e'}),"same prepare/request/publish order");
 c.order.clear();c.lock=spike::MaintenanceLock::Failed;check(spike::runMaintenance(c,true)==spike::MaintenanceResult::Failed,"invalid lock fails closed");check(c.order==std::vector<char>({'p','l'}),"failed gate must not issue work");
 Native native;std::atomic<bool> held{},release{};std::thread worker([&]{if(pthread_mutex_lock(&native.mutex))std::abort();held.store(true,std::memory_order_release);while(!release.load(std::memory_order_acquire))std::this_thread::yield();pthread_mutex_unlock(&native.mutex);});while(!held.load(std::memory_order_acquire))std::this_thread::yield();
 const auto busy=spike::runMaintenance(native,true);release.store(true,std::memory_order_release);worker.join();
 check(busy==spike::MaintenanceResult::Deferred&&native.deferred==1&&native.submitted==0,"foreground returns while batch held by worker");
 check(spike::runMaintenance(native,true)==spike::MaintenanceResult::Submitted&&native.submitted==1&&native.published==1,"debt executes once after queue is available");
 check(pthread_mutex_trylock(&native.mutex)==0,"publish releases admitted mutex");pthread_mutex_unlock(&native.mutex);
 std::printf("VENDOR_MAINTENANCE_PASS checks=%u real_native_mutex=true vendor_execution=false optical=NOT_RUN\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"VENDOR_MAINTENANCE_FAIL %s\n",e.what());return 1;}}
