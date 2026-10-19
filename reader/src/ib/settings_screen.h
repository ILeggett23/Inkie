// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Settings (mockup 25): a page list on the left, the chosen page's grouped rows on the right, under the usual mast.
// Every row is backed by a real setting or a real reading, and a reading that is unknown leaves its row out instead
// of inventing a value.
#include "ib/components.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
namespace ib::settings {
enum class Page:unsigned {General,WiFi,Battery,Storage,Security,Accessibility,Reading,Tom,About,Count};
const char* pageName(Page)noexcept;
struct Model {
 Page page{Page::Reading};
 // Reading
 bool effects{},toolHandle{true},leftHanded{},sleepCover{true},progress{true},snap{true};int sleepIdle{15};std::vector<int> idleChoices{0,5,10,15,30,60};
 // General: a user-ended session is left only with the power button (no Leave row then).
 bool userEnded{};
 // Device readings (unknown: -1 / empty, and the row is left out).
 int battery{-1};std::string charge; /* "Charging", "Full", "On battery", "Not charging" */ int externalPower{-1};
 int wifiLink{-1};
 std::int64_t storageTotal{-1},storageFree{-1};int documents{-1},notes{-1};
 std::string firmware,timeZone;
 // Security
 bool passcode{};int companion{-1}; /* -1 off on this device, 0 not paired, 1 pairing, 2 paired, 3 needs attention */
 // Accessibility
 bool highContrast{},reduceMotion{},plainText{};
 // Tom
 bool tomTyped{},tomReveal{true};unsigned tomHand{1};int tomOnline{-1};
 std::vector<std::pair<std::string,std::string>> about;
};
// Ids 950-965 are the ones the settings rows have always used (routes and tests tap them).
enum Id:int {Effects=950,Leave,Wifi,SleepCover,ToolHandle,LeftHanded,ReadingProgress,SnapHighlights,IdleBase=960,PageBase=970,
 TomStyleBase=980 /* +0 handwriting, +1 typed */,TomHandBase=982 /* +0..2 */,TomReveal=985,HighContrast=986,ReduceMotion=987,PasscodeSet=988,PasscodeOff=989,CompanionManage=990,TimeZone=991,PlainText=992};
inline constexpr Rect NavRect{80,300,388,2060},PaneRect{516,180,1510,2060};
void paint(Canvas&,Targets&,const Model&);
// "38.7 GB" style size: decimal gigabytes with one decimal (megabytes below 1 GB).
std::string size(std::int64_t bytes);
}
