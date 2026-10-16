/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <atomic>
namespace companion {void validateCurve(const QJsonObject&,QRectF);}
namespace tomink {QJsonArray curveInk(const QJsonObject& content,const QJsonObject& style,double scale,double ox,double oy,const std::atomic<bool>* stop=nullptr,double radius=1.75);}
