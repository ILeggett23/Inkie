// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "profile_loader_v2.h"
#include "e2_provisional_v2.h"
#include <string>
#ifndef INKBOUND_LIFECYCLE_PROFILE_SHA256
#define INKBOUND_LIFECYCLE_PROFILE_SHA256 ""
#endif
namespace recovery::v2::runtime {
// Build-time pin of the one externally qualified lifecycle profile. An empty
// or malformed pin is all-zero, which loadQualified() refuses: no default.
inline Digest qualifiedPin()noexcept{
 constexpr const char* hex=INKBOUND_LIFECYCLE_PROFILE_SHA256;Digest d{};
 auto value=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
 std::size_t n=0;for(;hex[n];++n){}
 if(n!=64)return {};
 for(std::size_t i=0;i<32;++i){int hi=value(hex[2*i]),lo=value(hex[2*i+1]);if(hi<0||lo<0)return {};d[i]=std::uint8_t(hi*16+lo);}
 return d;
}
inline std::string qualifiedPath(const std::string& appRoot){return appRoot+(e2::provisional?"/config/lifecycle-profile.provisional-e2":"/config/lifecycle-profile.qualified");}
// A provisional-e2 profile loads only in the E2 timing build and never otherwise.
inline Loaded loadPinned(const std::string& appRoot)noexcept{try{
 auto l=loadQualified(qualifiedPath(appRoot).c_str(),qualifiedPin(),0);
 if(l.profile&&(l.profile->qualification.rfind(e2::ProvisionalPrefix,0)==0)!=e2::provisional){Loaded refused;refused.failure=LoadFailure::Selection;return refused;}
 return l;}catch(...){return {};}}
}
