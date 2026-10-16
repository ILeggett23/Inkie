/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
namespace companion {void validateCompositionDraft(const QJsonObject&);QJsonObject lowerCompositionDraft(const QJsonObject&);void validateCompositionScene(const QJsonObject&);void validateObservedComposition(const QJsonObject&,const QJsonArray&);}
namespace tomink {void validateCompositePixels(const QJsonObject&,const QByteArray&);}
