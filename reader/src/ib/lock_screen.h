// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Passcode screen (Settings > Security): unlocking, choosing, confirming or checking the passcode. A full page in the
// native style: wordmark, prompt, one dot per digit, a 3x4 keypad of large square keys. Nothing of the book shows.
#include "ib/components.h"
#include <string>
namespace ib::lock {
struct Model {std::string prompt{"Enter passcode"},message;int entered{},length{4};bool cancel{},waiting{};};
enum Id:int {KeyBase=1100 /* +digit */,Back=1110,Cancel=1111};
inline constexpr Rect DotsRect{510,640,1110,740},MessageRect{160,760,1460,840},KeypadRect{390,900,1230,1980};
void paint(Canvas&,Targets&,const Model&);
}
