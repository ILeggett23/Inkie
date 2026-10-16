// SPDX-License-Identifier: AGPL-3.0-only
#include "vendor.h"
#include "underlay.h"
#include "ux_display.h"
#include "contract.h"
#include "release_policy.h"
#include "lifecycle_v2.h"
#include "dispatch_v2.h"
#include "qualified_pin_v2.h"
#include "session_power_v2.h"
#include "e2_provisional_v2.h"
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <string_view>
#include <unistd.h>
namespace {
// V2 reader role: runs only inside the owner-created reader unit with the one
// build-pinned qualified profile. Grants gate the UNCHANGED session loop; the
// final report is delivered separately and proves durability only if bound.
int lifecycleReader(const std::filesystem::path& root,const std::string& session){
 namespace lc=reader::lifecycle;using namespace recovery::v2;
 auto loaded=runtime::loadPinned(root.string());const char* env=getenv("INKBOUND_RECOVERY_V2_SESSION");
 if(!loaded.profile||!loaded.route||!loaded.sustained||!loaded.sessionPower||!env||session!=env){std::fputs("V2_READER_REFUSED: no build-pinned qualified lifecycle profile or session\n",stderr);return 78;}
 const auto& profile=*loaded.profile;
 // Power key / cover resolved by device NAME before any bootstrap; missing or ambiguous refuses.
 namespace pw=recovery::v2::sessionpower;const auto& sp=*loaded.sessionPower;
 const auto powerKey=pw::findInput("/sys","/dev/input",sp.powerKey),cover=sp.cover.empty()?std::filesystem::path{}:pw::findInput("/sys","/dev/input",sp.cover);
 if(powerKey.empty()||(!sp.cover.empty()&&cover.empty())){std::fputs("V2_READER_REFUSED: power key or cover input not uniquely found\n",stderr);return 78;}
 e2::mark("reader:bootstrap-begin");auto permit=dispatch::readerBootstrap(profile,session);e2::mark("reader:bootstrap-end");
 if(!permit){std::fputs("V2_READER_REFUSED: bootstrap\n",stderr);return 78;}
 const auto& offer=permit->offer;lc::Domains domains(profile.timing,offer.started.generation);
 if(!domains.valid()){close(permit->fd);std::fputs("V2_READER_REFUSED: domain map\n",stderr);return 78;}
 progress::MainProgressChannel channel;lc::Foreground foreground(domains,channel);
 // D2 (user-ended sessions): no grants and no ink permission. The permit above means the stock app
 // has stopped; from here the reader accepts ink until the user presses the power button.
 lc::UserLease lease;
 // E2 phase-E timing build only: all reader data stays inside the supervised staging directory.
 const std::filesystem::path data=e2::provisional?std::filesystem::path("/home/root/inkbound-e2/E/data"):std::filesystem::path(ux::DataPath);
 // Connected features (companion import, Tom) follow the release policy: on unless the app ships the offline policy.
 bool connected=false;try{connected=release::policy(root).networking;}catch(...){}
 ux::Shell shell(root,data,true,connected,connected,domains.tracking(lc::Domain::Canonical),domains.tracking(lc::Domain::Notes),domains.tracking(lc::Domain::Tom),domains.tracking(lc::Domain::Preparation),domains.tracking(lc::Domain::Resume));
 shell.setUserEnded(true); // D2: only the power button ends the session
 int result=78;
 // Session-scoped kernel wakeup source (autosleep is enabled on the target), renewed only
 // while admitted with the grant lifetime as timeout, so it expires by itself if we die.
 pw::DisplayRail rail(pw::DisplayRailLength); /* outlives power: restored on wake and when the session ends */
 pw::SessionPower power("/sys/power",pw::ReaderWakeLock,profile.timing.grantLifetime,powerKey,cover,spike::monotonic()); /* D2: power key ends; cover/idle only sleep (Inkbound sleep settings) */
 power.setDisplayRail(&rail);
 if(power.valid()){e2::mark("reader:admitted");spike::VendorDisplay vendor(spike::realVendorCalls());ux::Display display(vendor,shell,&foreground,&power);result=spike::runDirectSession(lease,display);}
 e2::mark("reader:session-end",unsigned(result));lease.requestStop();bool finished=false;
 for(const auto end=spike::monotonic()+profile.timing.drainLead;;){foreground.publish(shell.failed());if(shell.finish(10)){finished=true;break;}if(spike::monotonic()>=end)break;}
 e2::mark("reader:drain-end",finished);if(!finished)e2::mark("reader:drain-blockers",shell.drainBlockers());foreground.requestShutdown();foreground.publish(shell.failed());
 const lc::ApplicationState app{true,true,shell.failed(),shell.failed()||!finished,shell.semantics().failed(),shell.unsaved(),shell.semantics().pending()};
 auto report=lc::finalReport(domains,offer.identity,offer.generation,0,app); // D2: no grants
 const bool delivered=dispatch::sendReport(session,report),durable=quiescence::durable(report);
 e2::mark("reader:report-sent",delivered);power.release();
 std::fprintf(stderr,"V2_READER_STATUS result=%d finished=%d report_delivered=%d report_durable=%d unsaved=%u\n",result,finished,delivered,durable,shell.unsaved());
 return result||!durable?1:0;
}
}
int main(int argc,char**argv){
 // D2 long sessions: page-sized buffers (about 14 MB each) are allocated per saved stroke and per page. A fixed mmap
 // threshold returns every such buffer to the system when freed (glibc's adaptive threshold otherwise moves them into
 // the heap, where RSS only grew: 160 MB to 750 MB in E2 session 17), and few arenas limit per-thread retention.
 mallopt(M_MMAP_THRESHOLD,256*1024);mallopt(M_ARENA_MAX,4);
 const bool v2Reader=argc==3&&std::string_view(argv[1])=="--v2-reader";
 if(!v2Reader&&(argc!=2||(std::string_view(argv[1])!="--direct"&&std::string_view(argv[1])!="--arm"&&std::string_view(argv[1])!="--offline-arm"))){
  std::puts("Inkbound direct-handwriting-spike3: permit-gated direct payload; separate physical approval required.");return 78;
 }
 try{
 auto root=std::filesystem::path(recovery::executable()).parent_path().parent_path();
 // V2 admits nothing without the build-pinned qualified profile (none exists today).
 if(v2Reader)return lifecycleReader(root,argv[2]);
 // Packaged launch cannot fall through to the historical five-contact V1 route.
 // Replace this refusal only with the reviewed production V2 entry integration.
 if(std::string_view(argv[1])=="--offline-arm"||release::policy(root).packaged){std::fputs("BUILD_ONLY: sustained lifecycle is not qualified; launch refused before ownership or device access.\n",stderr);return 78;}
 if(std::string_view(argv[1])=="--arm"){if(std::filesystem::exists("/run/inkbound-owner3/active"))return 78;underlay::privateDirectory(ux::DataPath);std::atomic<bool> stop{false};reader::syncLibrary(root,ux::DataPath,stop);return spike::armOwnedSession();}
 auto lease=spike::receiveQualifiedRecoveryLease();
 if(!lease){std::fputs("RECOVERY_GATE_BLOCKED: no valid current-session owner permit; no vendor or input action.\n",stderr);return 78;}
 ux::Shell shell(root,ux::DataPath,true,false,false);spike::VendorDisplay vendor(spike::realVendorCalls());ux::Display display(vendor,shell);
 int result=spike::runDirectSession(*lease,display);bool finished=shell.finish(100);
 std::fprintf(stderr,"READER_UX_STATUS page=%d committed=%u unsaved=%u ui_rejections=%u failed=%d flush_completed=%d\n",shell.page(),shell.committed(),shell.unsaved(),shell.rejected(),shell.failed(),finished);
 std::fprintf(stderr,"SEMANTIC_STATUS highlight_acks=%u vocabulary_acks=%u pending=%u failed=%d\n",shell.semantics().highlightAcks(),shell.semantics().vocabularyAcks(),shell.semantics().pending(),shell.semantics().failed());
 if(!finished)std::_Exit(1);return result||shell.failed()||shell.unsaved()?1:0;
 }catch(const std::exception& e){std::fprintf(stderr,"UNDERLAY_START_FAILED reason=%s\n",e.what());return 78;}
}
