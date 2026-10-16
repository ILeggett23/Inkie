// SPDX-License-Identifier: AGPL-3.0-only
#include "vendor.h"
#include "underlay.h"
#include <cstdio>
#include <string_view>
int main(int argc,char**argv){
 if(argc!=2||(std::string_view(argv[1])!="--direct"&&std::string_view(argv[1])!="--arm")){
  std::puts("Inkbound direct-handwriting-spike3: permit-gated direct payload; separate physical approval required.");return 78;
 }
 if(std::string_view(argv[1])=="--arm"){if(underlay::prepareSibling()!=0)return 78;return spike::armOwnedSession();}
 try{
 auto prepared=underlay::loadPrepared(std::filesystem::path(underlay::DataPath)/"prepared.bin");
 underlay::Pipeline pipeline(std::filesystem::path(underlay::DataPath)/"annotations.ink",prepared);
 auto lease=spike::receiveQualifiedRecoveryLease();
 if(!lease){std::fputs("RECOVERY_GATE_BLOCKED: no valid current-session owner permit; no vendor or input action.\n",stderr);return 78;}
 spike::VendorDisplay vendor(spike::realVendorCalls());underlay::ComposedDisplay display(vendor,prepared,pipeline);
 int result=spike::runDirectSession(*lease,display);bool finished=pipeline.finish(100);
 std::fprintf(stderr,"SAVE_STATUS committed=%u unsaved=%u queue_high_water=%u admission_rejections=%u failed=%d flush_completed=%d\n",pipeline.committed.load(),pipeline.unsaved(),pipeline.highWater.load(),pipeline.rejected.load(),pipeline.failed(),finished);
 if(!finished)std::_Exit(1);return result||pipeline.failed()||pipeline.unsaved()?1:0;
 }catch(const std::exception& e){std::fprintf(stderr,"UNDERLAY_START_FAILED reason=%s\n",e.what());return 78;}
}
