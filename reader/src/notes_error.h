/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <string_view>
namespace notes {
inline std::string_view diagnosticReason(std::string_view reason)noexcept{if(reason.empty()||reason.size()>120)return "unexpected-exception";bool prefix=false;for(auto p:{"product-","note-","notes-","native-","tom-","styled-","underlay-"})prefix|=reason.starts_with(p);if(!prefix)return "unexpected-exception";for(char c:reason)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return "unexpected-exception";return reason;}
}
