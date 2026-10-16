// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QDateTime>
#include <QTimeZone>
#include <QString>
#include <filesystem>
namespace clockprefs {
struct Setting {QString zone;bool operator==(const Setting&)const=default;};
struct Reading {QString time,abbreviation;};
bool valid(const QString&)noexcept;Setting load(const std::filesystem::path&)noexcept;bool save(const std::filesystem::path&,const Setting&)noexcept;Reading at(qint64,const Setting&);QString label(const Setting&);
}
