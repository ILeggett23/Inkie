/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <QJsonObject>
#include <QRectF>
namespace companion {
void validateStructuredVisual(const QJsonObject&);
void validateVisualBindings(const QJsonObject&,const QJsonObject& expectedDatasets);
}
