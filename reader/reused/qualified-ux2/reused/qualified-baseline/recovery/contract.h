// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <sys/types.h>
namespace recovery {
using Ns=std::uint64_t;
Ns now();std::string boot();bool hex32(const std::string&);std::string nonce();
#ifdef OWNER_VM_TEST
inline constexpr const char* Base="/run/inkbound-owner3-test";
#else
inline constexpr const char* Base="/run/inkbound-owner3";
#endif
inline std::string ownerUnit(const std::string&s){return "inkbound-owner3-"+s+".service";}
inline std::string childUnit(const std::string&s){return "inkbound-direct3-"+s+".service";}
inline std::string socketPath(const std::string&s){return std::string(Base)+"/"+s+"/owner.sock";}
enum class Kind:std::uint32_t {Launcher=1,Ready,Commit,Granted,Child,Permit,Stop,Witness,Binding,Receipt,Ack,Recover,DropReference};
struct Binding {
 char session[33]{},boot[37]{},unit[96]{},owner[96]{},invocation[33]{},ownerInvocation[33]{},group[256]{},mount[128]{};
 std::uint64_t device{},inode{},deadline{};std::uint32_t ownerPid{},mainPid{};
 std::array<std::uint8_t,32> ticket{};
};
struct Packet {std::uint32_t magic=0x33424e49,version=1;Kind kind{};std::uint32_t closed{},drained{};Binding binding{};};
static_assert(sizeof(Packet)<1024);
inline bool same(const Binding&a,const Binding&b){return std::memcmp(&a,&b,sizeof(a))==0;}
struct State {std::string boot,id,invocation,load,active,sub,group;std::uint32_t main{},control{},job{};bool operator==(const State&)const=default;};
bool terminal(const State&);
bool complete(const Binding&,const Packet&,const State&,bool reference,bool closed,bool absent,bool authenticated);
void copy(char*,std::size_t,const std::string&);
bool validBinding(const Binding&);
int connectOwner(const std::string&,int timeoutMs);int listenOwner(const std::string&);
bool sendPacket(int,const Packet&);bool receivePacket(int,Packet&,int timeoutMs);pid_t peer(int);bool connected(int);
std::string executable();std::string readSmall(const std::string&,std::size_t max=8192);void writeFile(const std::string&,const std::string&);
std::string managerMount();bool absentGroup(const Binding&);bool membershipWitness(const Binding&,pid_t);bool inspectSubtree(int,pid_t,std::uint64_t);
}
