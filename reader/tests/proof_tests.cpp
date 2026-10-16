// SPDX-License-Identifier: AGPL-3.0-only
#include "contract.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using namespace recovery;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);abort();}}while(0)
struct Fixture {Binding b{};Packet r;State s;bool ref=true,closed=true,absent=true,auth=true;
 Fixture(){copy(b.session,sizeof b.session,std::string(32,'a'));copy(b.boot,sizeof b.boot,"00000000-0000-0000-0000-000000000001");copy(b.unit,sizeof b.unit,childUnit(b.session));copy(b.owner,sizeof b.owner,ownerUnit(b.session));copy(b.invocation,sizeof b.invocation,std::string(32,'b'));copy(b.ownerInvocation,sizeof b.ownerInvocation,std::string(32,'c'));copy(b.group,sizeof b.group,"/system.slice/"+childUnit(b.session));copy(b.mount,sizeof b.mount,"/sys/fs/cgroup/unified");b.device=27;b.inode=43;b.deadline=100;b.ownerPid=100;b.mainPid=101;b.ticket[0]=1;r.kind=Kind::Receipt;r.closed=r.drained=1;r.binding=b;s.boot=b.boot;s.id=b.unit;s.invocation=b.invocation;s.load="loaded";s.active="inactive";s.sub="dead";}
 bool ok(){return complete(b,r,s,ref,closed,absent,auth);}
};
int main(){unsigned count=0;auto run=[&](const char*n,std::function<void()>f){f();printf("PASS %s\n",n);++count;};
 run("valid_after_kernel_pruning",[]{Fixture f;CHECK(f.ok());});
 run("invocation_reuse",[]{Fixture f;f.s.invocation=std::string(32,'d');CHECK(!f.ok());});
 run("unit_reuse",[]{Fixture f;f.s.id="other.service";CHECK(!f.ok());});
 run("retained_or_populated_group",[]{Fixture f;f.absent=false;CHECK(!f.ok());});
 run("live_main",[]{Fixture f;f.s.main=101;CHECK(!f.ok());});
 run("live_control",[]{Fixture f;f.s.control=102;CHECK(!f.ok());});
 run("unresolved_job",[]{Fixture f;f.s.job=4;CHECK(!f.ok());});
 run("missing_receipt",[]{Fixture f;f.r=Packet{};CHECK(!f.ok());});
 run("wrong_receipt_kind",[]{Fixture f;f.r.kind=Kind::Permit;CHECK(!f.ok());});
 run("stale_receipt_session",[]{Fixture f;f.r.binding.session[0]='b';CHECK(!f.ok());});
 run("wrong_receipt_inode",[]{Fixture f;++f.r.binding.inode;CHECK(!f.ok());});
 run("wrong_boot",[]{Fixture f;f.s.boot="00000000-0000-0000-0000-000000000002";CHECK(!f.ok());});
 run("open_admission",[]{Fixture f;f.closed=false;CHECK(!f.ok());});
 run("receipt_not_closed",[]{Fixture f;f.r.closed=0;CHECK(!f.ok());});
 run("witness_not_drained",[]{Fixture f;f.r.drained=0;CHECK(!f.ok());});
 run("unauthenticated_witness",[]{Fixture f;f.auth=false;CHECK(!f.ok());});
 run("owner_reference_loss",[]{Fixture f;f.ref=false;CHECK(!f.ok());});
 run("manager_disconnect_no_reacquire",[]{Fixture f;f.ref=false;f.s.load="loaded";CHECK(!f.ok());});
 run("premature_collection",[]{Fixture f;f.s.load="not-found";CHECK(!f.ok());});
 run("failed_is_not_death",[]{Fixture f;f.s.active="failed";f.s.sub="failed";f.s.group=f.b.group;CHECK(!f.ok());});
 run("D_state_descendant",[]{Fixture f;f.s.active="failed";f.s.sub="failed";f.absent=false;CHECK(!f.ok());});
 run("malformed_nonce",[]{Fixture f;f.b.session[0]='/';CHECK(!f.ok());});
 run("group_traversal",[]{Fixture f;copy(f.b.group,sizeof f.b.group,"/system.slice/../other");CHECK(!f.ok());});
 run("no_ticket",[]{Fixture f;f.b.ticket.fill(0);f.r.binding=f.b;CHECK(!f.ok());});
 run("wrong_ticket",[]{Fixture f;f.r.binding.ticket[0]=2;CHECK(!f.ok());});
 run("receipt_does_not_renew_deadline",[]{Fixture f;++f.r.binding.deadline;CHECK(!f.ok());});
 run("zero_deadline",[]{Fixture f;f.b.deadline=0;f.r.binding=f.b;CHECK(!f.ok());});
 run("malformed_receipt",[]{Fixture f;f.r.magic=0;CHECK(!f.ok());});
 char tmp[]="/tmp/owner-proof-XXXXXX";CHECK(mkdtemp(tmp));std::filesystem::path p(tmp);auto write=[&](std::string s){std::ofstream(p/"cgroup.procs")<<s;};auto scan=[&]{int fd=open(tmp,O_RDONLY|O_DIRECTORY);struct stat st{};CHECK(!fstat(fd,&st));bool ok=inspectSubtree(fd,getpid(),st.st_dev);close(fd);return ok;};
 run("witness_only_member",[&]{write(std::to_string(getpid())+"\n");CHECK(scan());});
 run("foreign_process",[&]{write(std::to_string(getpid())+"\n99999\n");CHECK(!scan());});
 run("malformed_membership",[&]{write("not-a-pid\n");CHECK(!scan());});
 run("unreadable_membership",[&]{write("");chmod((p/"cgroup.procs").c_str(),0);CHECK(!scan());chmod((p/"cgroup.procs").c_str(),0600);});
 run("nested_descendant",[&]{write("");std::filesystem::create_directory(p/"sub");std::ofstream(p/"sub/cgroup.procs")<<"99999\n";CHECK(!scan());std::filesystem::remove_all(p/"sub");});
 run("symlink_escape",[&]{write("");std::filesystem::create_directory_symlink("/tmp",p/"escape");CHECK(!scan());std::filesystem::remove(p/"escape");});
 run("traversal_limit",[&]{write("");auto sub=p;for(int i=0;i<10;++i){sub/="deep";std::filesystem::create_directory(sub);std::ofstream(sub/"cgroup.procs")<<"";}CHECK(!scan());std::filesystem::remove_all(p/"deep");});
 std::filesystem::remove_all(p);printf("RECOVERY_ADVERSARIAL_PASS cases=%u\n",count);
}
