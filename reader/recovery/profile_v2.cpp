// SPDX-License-Identifier: AGPL-3.0-only
#include "profile_v2.h"
#include <algorithm>
#include <limits>
#include <signal.h>
#include <linux/capability.h>
namespace recovery::v2::runtime {
namespace {
bool label(const std::string& s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;});}
bool hex(const std::string& s){return s.size()==32&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
bool includes(const std::vector<std::string>& actual,const std::vector<std::string>& wanted){return std::all_of(wanted.begin(),wanted.end(),[&](const auto& v){return std::count(actual.begin(),actual.end(),v)==1;});}
}
bool valid(const Profile& p)noexcept{
 if(p.selection!=Selection::Synthetic&&p.selection!=Selection::Qualified)return false;
 if(!label(p.qualification)||!label(p.managerVersion)||!label(p.powerRoutes)||!validTiming(p.timing)||!fuse::validPolicy(p.fuse,p.timing))return false;
 // Ceilings are fixed for the entire session. Only the independent fuse is
 // renewable, so even an otherwise healthy session ends at its selected cap.
 constexpr Ns max=std::numeric_limits<Ns>::max()/4;
 if(!p.callBudget||!p.startBudget||!p.stopBudget||p.readerCeiling>max||p.ownerCeiling>max)return false;
 if(p.callBudget>=p.timing.challengeLifetime||p.startBudget>p.timing.startupBudget||p.stopBudget>p.timing.killGrace)return false;
 if(p.readerCeiling<=p.timing.startupBudget+p.timing.grantLifetime||p.ownerCeiling<=p.readerCeiling+p.timing.proofWindow+2*p.callBudget)return false;
 for(auto n:{p.readerCeiling,p.ownerCeiling,p.callBudget,p.startBudget,p.stopBudget,p.fuse.bootstrapLifetime})if(n<1000||n%1000)return false;
 return true;
}
bool allowsStock(const Profile& p,const std::string& session,const std::string& stock)noexcept{
 if(!valid(p)||!hex(session))return false;
 return p.selection==Selection::Synthetic?stock=="inkbound-stock-v2-test-"+session+".service":stock=="xochitl.service";
}
UnitProfile requested(const Profile& p,Role role,const std::string& owner,const std::string& fuseName){
 if(!valid(p))return {};
 UnitProfile v;v.type=role==Role::Fuse?"notify":"exec";v.notifyAccess=role==Role::Fuse?"main":"none";v.restart="no";v.killMode="control-group";
 // D2: the fuse is a backstop for the whole session (no timed termination of the reader).
  v.runtimeUsec=(role==Role::Reader?p.readerCeiling:p.ownerCeiling)/1000;
 v.startUsec=p.startBudget/1000;v.stopUsec=p.stopBudget/1000;v.abortUsec=v.stopUsec;v.killSignal=SIGKILL;v.finalKillSignal=SIGKILL;v.sendSigkill=true;v.noNewPrivileges=true;
 // Only the reader may hold a session-scoped kernel wakeup source (autosleep is
 // enabled on the target); owner and fuse keep an empty bounding set.
 if(role==Role::Reader)v.capabilities=std::uint64_t(1)<<CAP_BLOCK_SUSPEND;
 if(role!=Role::Owner)v.bindsTo.push_back(owner);
 if(role==Role::Reader)v.bindsTo.push_back(fuseName);
 v.after=v.bindsTo;return v;
}
bool matches(const UnitProfile& q,const UnitProfile& a)noexcept{
 return !q.type.empty()&&q.type==a.type&&q.notifyAccess==a.notifyAccess&&q.restart==a.restart&&q.killMode==a.killMode&&q.runtimeUsec==a.runtimeUsec&&q.startUsec==a.startUsec&&q.stopUsec==a.stopUsec&&q.abortUsec==a.abortUsec&&q.capabilities==a.capabilities&&q.killSignal==a.killSignal&&q.finalKillSignal==a.finalKillSignal&&q.sendSigkill==a.sendSigkill&&q.noNewPrivileges==a.noNewPrivileges&&q.delegate==a.delegate&&includes(a.bindsTo,q.bindsTo)&&includes(a.after,q.after);
}
}
