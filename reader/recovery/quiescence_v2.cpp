// SPDX-License-Identifier: AGPL-3.0-only
#include "quiescence_v2.h"
#include <algorithm>
#include <cstring>
namespace recovery::v2::quiescence {
namespace {
constexpr std::uint8_t Magic[4]={'I','B','Q','R'};
constexpr std::uint16_t Version=1;
constexpr std::size_t Used=399;
bool hex32(const std::array<char,33>& s){for(std::size_t i=0;i<32;++i){char c=s[i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;}return s[32]==0;}
struct Writer {
 Wire& out;std::size_t at{};
 void bytes(const void* p,std::size_t n){std::memcpy(out.data()+at,p,n);at+=n;}
 void u(std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)out[at++]=std::uint8_t(v>>(8*i));}
};
struct Reader {
 std::span<const std::uint8_t> in;std::size_t at{};
 void bytes(void* p,std::size_t n){std::memcpy(p,in.data()+at,n);at+=n;}
 std::uint64_t u(unsigned n){std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(in[at++])<<(8*i);return v;}
};
}
bool durable(const Report& r)noexcept{
 if(!r.inputClosed||!r.admissionClosed||!r.helpersReconciled||r.saveFailed||r.shellFailed||r.semanticFailed||r.unsaved||r.semanticPending)return false;
 for(std::size_t n=0;n<Domains;++n){const auto& d=r.domains[n];if(r.faults[n]||d.backlog||d.accepted!=d.completed)return false;}
 return true;
}
// The owner's proof counts grants SENT; the report counts grants ACCEPTED. The child answers a
// challenge only when no grant is outstanding, so at most one grant can be in flight when the
// child seals itself at quiescence (observed on the Paper Pro, E2 phase A). An unaccepted grant
// authorizes nothing, so exactly one behind is the same session state; ahead or further behind is not.
static bool graceful(std::uint64_t reported,std::uint64_t proved)noexcept{return reported==proved||(proved>0&&reported==proved-1);}
bool bound(const Report& r,const SessionIdentity& id,std::uint64_t generation,std::uint64_t clockGeneration,const ClosureProof& closure)noexcept{
 return hex32(r.session)&&hex32(r.invocation)&&r.session==id.session&&r.invocation==id.invocation&&r.ticket==id.ticket&&r.ticket!=Nonce{}&&
  generation&&r.generation==generation&&clockGeneration&&r.clockGeneration==clockGeneration&&
  closure.identity==id&&closure.generation==generation&&graceful(r.lastGrantSequence,closure.lastGrantSequence);
}
const char* mismatch(const Report& r,const SessionIdentity& id,std::uint64_t generation,std::uint64_t clockGeneration,const ClosureProof& closure)noexcept{
 if(!hex32(r.session)||r.session!=id.session)return "session";if(!hex32(r.invocation)||r.invocation!=id.invocation)return "invocation";
 if(r.ticket!=id.ticket||r.ticket==Nonce{})return "ticket";if(!generation||r.generation!=generation)return "generation";
 if(!clockGeneration||r.clockGeneration!=clockGeneration)return "clock-generation";if(!(closure.identity==id))return "closure-identity";
 if(closure.generation!=generation)return "closure-generation";if(!graceful(r.lastGrantSequence,closure.lastGrantSequence))return "grant-sequence";
 return "";
}
bool encode(const Report& r,Wire& out)noexcept{
 out.fill(0);
 if(!hex32(r.session)||!hex32(r.invocation)||!r.generation||!r.clockGeneration)return false;
 Writer w{out};w.bytes(Magic,4);w.u(Version,2);w.u(0,2);
 w.bytes(r.session.data(),33);w.bytes(r.invocation.data(),33);w.bytes(r.ticket.data(),32);
 w.u(r.generation,8);w.u(r.clockGeneration,8);w.u(r.lastGrantSequence,8);
 for(const auto& d:r.domains){w.u(d.accepted,8);w.u(d.completed,8);w.u(d.backlog,4);w.u(d.oldest.monotonic,8);w.u(d.oldest.boottime,8);w.u(d.oldest.generation,8);}
 for(auto f:r.faults)w.u(f,8);
 w.u(r.unsaved,4);w.u(r.semanticPending,4);
 w.u(unsigned(r.inputClosed)|unsigned(r.admissionClosed)<<1|unsigned(r.helpersReconciled)<<2|unsigned(r.saveFailed)<<3|unsigned(r.shellFailed)<<4|unsigned(r.semanticFailed)<<5,1);
 return w.at==Used;
}
std::optional<Report> decode(std::span<const std::uint8_t> in)noexcept{
 if(in.size()!=WireBytes||std::memcmp(in.data(),Magic,4))return {};
 Reader r{in,4};if(r.u(2)!=Version||r.u(2))return {};
 Report out;r.bytes(out.session.data(),33);r.bytes(out.invocation.data(),33);r.bytes(out.ticket.data(),32);
 out.generation=r.u(8);out.clockGeneration=r.u(8);out.lastGrantSequence=r.u(8);
 for(auto& d:out.domains){d.accepted=r.u(8);d.completed=r.u(8);d.backlog=std::uint32_t(r.u(4));d.oldest.monotonic=r.u(8);d.oldest.boottime=r.u(8);d.oldest.generation=r.u(8);}
 for(auto& f:out.faults)f=r.u(8);
 out.unsaved=std::uint32_t(r.u(4));out.semanticPending=std::uint32_t(r.u(4));
 auto flags=r.u(1);if(flags>>6)return {};
 out.inputClosed=flags&1;out.admissionClosed=flags&2;out.helpersReconciled=flags&4;out.saveFailed=flags&8;out.shellFailed=flags&16;out.semanticFailed=flags&32;
 for(std::size_t n=Used;n<in.size();++n)if(in[n])return {};
 if(!hex32(out.session)||!hex32(out.invocation)||!out.generation||!out.clockGeneration)return {};
 Wire again;if(!encode(out,again)||!std::equal(again.begin(),again.end(),in.begin()))return {};
 return out;
}
}
