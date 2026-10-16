// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <exception>
#include <initializer_list>
#include <string_view>
namespace reader::lifecycle {
// These are parent-side control-flow outcomes, never helper stderr or document
// text. The *-failed sites occur only after waitpid and transport reconciliation;
// identity/actions/spawn failures occur before any helper is owned. The pinned
// page_helper.cpp is unchanged. Unknown wait/cleanup failures are NOT proof.
inline bool reconciledHelperFailure(const std::exception& error)noexcept {
 const std::string_view reason=error.what();
 for(auto known:{"page-helper-failed","page-helper-identity","page-helper-actions","page-helper-spawn","page-helper-document-format","page-helper-view-page-range","library-helper-failed","library-helper-identity","library-helper-actions","library-helper-spawn","epub-stage-helper-failed","epub-stage-helper-identity","epub-stage-actions","epub-stage-spawn"})if(reason==known)return true;
 return false;
}
}
