/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <QJsonObject>
#include <QRectF>
#include <QString>
namespace companion {
void validatePicturePlan(const QJsonObject&);
QJsonObject lowerPicturePlan(const QJsonObject&);
QJsonObject pictureProjection(QJsonObject);
void validatePictureScene(const QJsonObject&);
QString picturePrompt(const QJsonObject&);
void validatePictureObservation(const QJsonObject&,const QJsonObject&);
QJsonObject pictureLabelLayout(const QJsonObject&,bool includeUnlocated); /* transient compositor output; transport validation always precedes it */
QJsonObject applyPictureAdvice(const QJsonObject&,const QJsonObject&);
QJsonObject mapPictureGeometry(const QJsonObject&,QRectF);
}
