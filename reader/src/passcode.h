// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Settings > Security: Inkbound's passcode lock. The 4-digit passcode is kept only as a salted, iterated SHA-256 digest
// (ui-passcode.json, private to the user). It guards the screen while Inkbound runs: the lock is shown when Inkbound
// starts, before the tablet sleeps and when it wakes. It does not encrypt the library; with only 10,000 codes the
// protection is the attempt limit (5 wrong tries, then a 30 s wait), not the digest.
#include <QByteArray>
#include <QString>
#include <filesystem>
#include <optional>
namespace security {
inline constexpr int PasscodeLength=4,MaxAttempts=5,LockoutSeconds=30;
struct Passcode {QByteArray salt,digest;int iterations{};};
bool validCode(const QString&)noexcept;                       // exactly four ASCII digits
QByteArray derive(const QString& code,const QByteArray& salt,int iterations);
std::optional<Passcode> load(const std::filesystem::path& root)noexcept; // nothing when no passcode is set (or unreadable)
void save(const std::filesystem::path& root,const QString& code);       // new random salt
void clear(const std::filesystem::path& root);
bool matches(const Passcode&,const QString& code)noexcept;             // constant-time comparison
}
