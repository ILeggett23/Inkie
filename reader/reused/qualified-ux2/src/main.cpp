// SPDX-License-Identifier: AGPL-3.0-only
#include "vendor.h"
#include "underlay.h"
#include "ux_display.h"
#include "contract.h"
#include <cstdio>
#include <string_view>
int main(int argc,char**argv){
 if(argc!=2||(std::string_view(argv[1])!="--direct"&&std::string_view(argv[1])!="--arm")){
  std::puts("Inkbound direct-handwriting-spike3: permit-gated direct payload; separate physical approval required.");return 78;
 }
 try{
 auto root=std::filesystem::path(recovery::executable()).parent_path().parent_path();
 if(std::string_view(argv[1])=="--arm"){if(std::filesystem::exists("/run/inkbound-owner3/active")||!ux::catalogueValid(root))return 78;underlay::privateDirectory(ux::DataPath);return spike::armOwnedSession();}
 auto lease=spike::receiveQualifiedRecoveryLease();
 if(!lease){std::fputs("RECOVERY_GATE_BLOCKED: no valid current-session owner permit; no vendor or input action.\n",stderr);return 78;}
 ux::Shell shell(root,ux::DataPath);spike::VendorDisplay vendor(spike::realVendorCalls());ux::Display display(vendor,shell);
 int result=spike::runDirectSession(*lease,display);bool finished=shell.finish(100);
 std::fprintf(stderr,"READER_UX_STATUS page=%d committed=%u unsaved=%u ui_rejections=%u failed=%d flush_completed=%d\n",shell.page(),shell.committed(),shell.unsaved(),shell.rejected(),shell.failed(),finished);
 if(!finished)std::_Exit(1);return result||shell.failed()||shell.unsaved()?1:0;
 }catch(const std::exception& e){std::fprintf(stderr,"UNDERLAY_START_FAILED reason=%s\n",e.what());return 78;}
}
