// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>
namespace notebook {
enum class Paper:unsigned {Blank,Lined,Grid,Dots};
// A minimal, self-contained PDF of page-count template pages (3:4, vector rules
// in light gray) for the normal import path. Pure C++; no external input.
std::string pdf(Paper,int pages);
}
