// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
namespace ux {
// Extent and quality are independent. UI replacement, color, area and region
// count never authorize the complete-update bit. These are application reasons,
// not recovered Xochitl thresholds or vendor waveform values.
enum class FullRefreshReason:unsigned {
 None, Initialization, DocumentBinding, Security, Sleep, Wake, Recovery
};
constexpr bool completeRefreshAllowed(FullRefreshReason reason)noexcept{
 switch(reason){
 case FullRefreshReason::Initialization:case FullRefreshReason::DocumentBinding:
 case FullRefreshReason::Security:case FullRefreshReason::Sleep:
 case FullRefreshReason::Wake:case FullRefreshReason::Recovery:return true;
 default:return false;
 }
}
constexpr const char* fullRefreshReasonName(FullRefreshReason reason)noexcept{
 switch(reason){
 case FullRefreshReason::Initialization:return "initialization";
 case FullRefreshReason::DocumentBinding:return "document-binding";
 case FullRefreshReason::Security:return "security";
 case FullRefreshReason::Sleep:return "sleep";
 case FullRefreshReason::Wake:return "wake";
 case FullRefreshReason::Recovery:return "recovery";
 default:return "none";
 }
}
struct RefreshConditions {bool pen{},erasing{},saving{},animating{},quiet{};};
constexpr bool refreshSettled(RefreshConditions c)noexcept{return !c.pen&&!c.erasing&&!c.saving;}
enum class SceneRefresh {None,Deferred,Regional,Complete};
}
