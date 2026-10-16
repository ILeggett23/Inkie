// SPDX-License-Identifier: AGPL-3.0-only
#include "vendor.h"
#include "underlay.h"
#include "reader_display.h"
#include "contract.h"
#include <cstdio>
#include <string_view>
int main(int argc,char**argv){
 if(argc!=2||(std::string_view(argv[1])!="--direct"&&std::string_view(argv[1])!="--arm")){
  std::puts("Inkbound direct-handwriting-spike3: permit-gated direct payload; separate physical approval required.");return 78;
 }
 if(std::string_view(argv[1])=="--arm"){if(underlay::prepareSibling()!=0)return 78;return spike::armOwnedSession();}
 try{
 auto prepared=underlay::loadPrepared(std::filesystem::path(underlay::DataPath)/"current.prepared");
 reader::Reader document(std::filesystem::path(recovery::executable()).parent_path().parent_path(),underlay::DataPath,std::move(prepared));
 auto lease=spike::receiveQualifiedRecoveryLease();
 if(!lease){std::fputs("RECOVERY_GATE_BLOCKED: no valid current-session owner permit; no vendor or input action.\n",stderr);return 78;}
 spike::VendorDisplay vendor(spike::realVendorCalls());reader::ReaderDisplay display(vendor,document);
 int result=spike::runDirectSession(*lease,display);bool finished=document.finish(100);
 std::fprintf(stderr,"READER_STATUS page=%d pages=%d committed=%u unsaved=%u navigation_rejections=%u failed=%d flush_completed=%d\n",document.page(),document.total(),document.committed(),document.unsaved(),document.rejectedNavigation(),document.failed(),finished);
 if(!finished)std::_Exit(1);return result||document.failed()||document.unsaved()?1:0;
 }catch(const std::exception& e){std::fprintf(stderr,"UNDERLAY_START_FAILED reason=%s\n",e.what());return 78;}
}
