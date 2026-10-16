#include "companion_view.h"
#include <QCoreApplication>
#include <QDateTime>
#include <iostream>
#include <stdexcept>
#include <vector>
using ux::CompanionView;
using Action=CompanionView::Action;
static void check(bool v,const char*label){if(!v)throw std::runtime_error(label);std::cout<<label<<"=PASS\n";}
int main(int argc,char**argv){QCoreApplication app(argc,argv);const double now=double(QDateTime::currentSecsSinceEpoch());
 const QString id="12345678-1234-1234-1234-123456789abc",account="lower.Case+tag_name@example.com";
 QJsonObject claimed{{"state","approval-required"},{"pairing_id",id},{"pairing_code","ABCD1234"},{"account",account},{"expires_at",now+600},{"next_poll",0}};
 std::vector<std::uint32_t> pixels(1620*2160);spike::Surface surface{pixels,1620,2160,1620};
 CompanionView view;check(!view.active()&&view.displayedText().isEmpty()&&view.hit(200,1700).action==Action::None,"inactive_no_actions");
 view.open(claimed);check(view.active()&&view.hit(200,1700).action==Action::None,"approval_requires_painted_snapshot");view.paint(surface);
 check(view.displayedText().contains(account)&&view.displayedText().contains("ABCD1234"),"identity_and_code_exact_text");auto originalPixels=pixels;
 auto action=view.hit(200,1700);check(action.action==Action::Approve&&action.expectedPairId==id&&action.expectedAccount==account,"approval_exact_snapshot_identity");
 check(view.hit(200,1700).action==Action::None,"double_tap_blocked");view.update(claimed);view.paint(surface);check(view.hit(200,1700).action==Action::StartPair,"attempted_approval_never_replayed");
 CompanionView changed;changed.open(claimed);changed.paint(surface);auto other=claimed;other["pairing_id"]="22345678-1234-1234-1234-123456789abc";other["account"]="second@example.com";changed.update(other);check(changed.hit(200,1700).action==Action::None,"status_update_invalidates_display");changed.paint(surface);action=changed.hit(200,1700);check(action.action==Action::Approve&&action.expectedPairId==other["pairing_id"].toString()&&action.expectedAccount=="second@example.com","new_painted_identity_used");
 CompanionView rejected;rejected.open(claimed);rejected.paint(surface);action=rejected.hit(1100,1700);check(action.action==Action::Reject&&action.expectedPairId==id,"reject_separate_target");
 CompanionView busy;auto busyClaim=claimed;busyClaim["busy"]=true;busy.open(busyClaim);busy.paint(surface);check(busy.hit(200,1700).action==Action::None,"busy_approval_cannot_dispatch");check(busy.hit(1400,100).action==Action::Close,"busy_panel_can_close");
 CompanionView retry;QJsonObject retryState{{"state","transfer-error"},{"retry_available",true},{"next_poll",0}};retry.open(retryState);retry.paint(surface);check(retry.hit(200,1700).action==Action::RetryTransfer,"explicit_retry_command");retryState["retry_available"]=false;retry.open(retryState);retry.paint(surface);check(retry.hit(200,1700).action==Action::Sync,"imported_ack_uses_sync_not_reset");
 CompanionView unicode;auto unsupported=claimed;unsupported["account"]=QString::fromUtf8("josé@example.com");unicode.open(unsupported);unicode.paint(surface);check(unicode.hit(200,1700).action==Action::None&&unicode.displayedText().contains("APPROVAL DISABLED - ACCOUNT NAME UNSUPPORTED"),"unicode_identity_refuses_approval");
 unsupported["account"]="name with space@example.com";unicode.update(unsupported);unicode.paint(surface);check(unicode.hit(200,1700).action==Action::None,"unsupported_ascii_refuses_approval");
 CompanionView upper;auto uppercase=claimed;uppercase["account"]=account.toUpper();upper.open(uppercase);upper.paint(surface);check(pixels!=originalPixels,"lowercase_render_not_uppercased");
 CompanionView secret;auto injected=claimed;injected["device_token"]="NEVERRENDERDEVICESECRET";injected["pairing_token"]="NEVERRENDERPAIRSECRET";injected["download_token"]="NEVERRENDERDOWNLOADSECRET";secret.open(injected);secret.paint(surface);check(pixels==originalPixels&&!secret.displayedText().join(' ').contains("SECRET"),"token_fields_never_rendered");injected["error"]="NEVERRENDERERRORSECRET";secret.update(injected);secret.paint(surface);check(secret.displayedText().contains("CONNECTION ERROR - TRY AGAIN LATER")&&!secret.displayedText().join(' ').contains("SECRET"),"raw_error_not_reflected");
 CompanionView uncertain;auto state=claimed;state["state"]="approval-uncertain-repair-required";uncertain.open(state);uncertain.paint(surface);check(uncertain.hit(1100,1700).action==Action::None&&uncertain.hit(200,1700).action==Action::StartPair,"uncertain_only_fresh_pair");
 CompanionView pending;state=claimed;state["state"]="pairing";state["account"]=QJsonValue::Null;pending.open(state);pending.paint(surface);check(pending.hit(200,1700).action==Action::PollPair,"pairing_poll");
 CompanionView paired;state=claimed;state["state"]="paired";paired.open(state);paired.paint(surface);check(paired.hit(200,1700).action==Action::Sync,"paired_sync");
 /* Unpair needs a confirming second tap; the first only re-labels the button. */
 CompanionView unpair;unpair.open(state);unpair.paint(surface);check(unpair.displayedText().contains("UNPAIR"),"paired_offers_unpair");check(unpair.hit(1100,1700).action==Action::Changed,"unpair_first_tap_asks");unpair.paint(surface);check(unpair.hit(1100,1700).action==Action::Unpair,"unpair_second_tap_confirms");
 for(const auto* s:{"unpaired","expired","revoked-or-expired","pairing-expired-repair-required"}){CompanionView v;state={{"state",s}};v.open(state);v.paint(surface);check(v.hit(200,1700).action==Action::StartPair,"repair_state_fresh_pair");}
 CompanionView expired;state=claimed;state["expires_at"]=now-1;expired.open(state);expired.paint(surface);check(expired.hit(200,1700).action==Action::StartPair,"expired_claim_not_approved");
 CompanionView backoff;state=claimed;state["next_poll"]=now+120;backoff.open(state);backoff.paint(surface);check(backoff.hit(200,1700).action==Action::None&&backoff.hit(1100,1700).action==Action::None&&backoff.displayedText().join(' ').contains("BEFORE RETRY"),"backoff_blocks_network_actions");
 CompanionView transfer;state={{"state","transferring"},{"received",1024},{"total",2048}};transfer.open(state);transfer.paint(surface);check(transfer.displayedText().contains("RECEIVED 1024 OF 2048 BYTES")&&transfer.hit(200,1700).action==Action::None,"transfer_progress");
 CompanionView failed;failed.open({{"state","storage-error-reopen-required"}});failed.paint(surface);check(failed.hit(200,1700).action==Action::None&&failed.hit(1100,1700).action==Action::None,"storage_failure_blocks_actions");check(failed.hit(1400,90).action==Action::Close&&!failed.active(),"close_preserves_offline_reader");
 CompanionView invalid;state=claimed;state["pairing_id"]=id+'\n';invalid.open(state);invalid.paint(surface);check(invalid.hit(200,1700).action==Action::None,"malformed_pair_id_not_approved");
}
