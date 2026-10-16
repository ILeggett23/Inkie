// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QByteArray>
#include <QUuid>
#include <QJsonObject>
#include <atomic>
#include <filesystem>
#include <memory>
namespace tomink {
inline QString illustrationRequestId(const QString& parent){return QUuid::createUuidV5(QUuid("d01f6a69-7965-48a1-8ce8-1ac639327b25"),parent.toUtf8()).toString(QUuid::WithoutBraces);}
class IllustrationProvider {public:virtual ~IllustrationProvider()=default;virtual QByteArray generate(const QJsonObject&,const std::atomic<bool>& canceled)=0;};
std::shared_ptr<IllustrationProvider> companionIllustrations(std::filesystem::path application,std::filesystem::path library);
}
