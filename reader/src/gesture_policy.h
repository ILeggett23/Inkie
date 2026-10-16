#pragma once
#include "core.h"
// Finger long press (select a word). A fingertip on this panel drifts by a few pixels while it is held still, and a
// 12 px slop (1.3 mm at 229 ppi) cancelled many genuine presses, so selection by finger worked only some of the time.
// 24 px (2.7 mm) still stays under the 25 px at which touch.cpp treats a moving finger as a swipe. 500 ms matches common
// touch platforms; 650 ms read as unresponsive.
namespace semantic {inline constexpr spike::Ns LongPressNs=500000000;inline constexpr int LongPressSlop=24;}
