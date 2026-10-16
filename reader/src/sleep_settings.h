// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <filesystem>
#include <span>
namespace power {
// Inkbound's own sleep settings, applied while an Inkbound session runs (the stock app's settings are not touched).
// cover: closing the folio quiesces Inkbound and lets the tablet sleep. idleMinutes: sleep after this long without
// pen, touch or button input; 0 means never.
struct SleepSettings {bool cover{true};int idleMinutes{15};bool operator==(const SleepSettings&)const=default;};
inline constexpr int IdleChoices[]={0,5,10,15,30,60};
bool validIdle(int minutes)noexcept;
// The next or previous idle choice (clamped at the ends).
int stepIdle(int minutes,int delta)noexcept;
std::filesystem::path settingsPath(const std::filesystem::path& data);
// Missing, unreadable or malformed settings yield the defaults; never throws.
SleepSettings load(const std::filesystem::path& data)noexcept;
// Durable (atomic) write; false on any failure, leaving the previous file in place.
bool save(const std::filesystem::path& data,const SleepSettings&)noexcept;
}
