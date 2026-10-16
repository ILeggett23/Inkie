// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <atomic>
#include <functional>
#include <vector>
namespace nativeink {
inline constexpr unsigned MaxStrokes=1536,MaxPoints=98304,MaxPathPoints=2048;
inline constexpr unsigned MaxVisualStrokes=1920,MaxVisualPoints=122880;
struct StrokeTiming {double complexity{1};unsigned pauseAfterMs{};};
struct Group {QJsonArray strokes;QString kind{"tom_handwriting"};QRectF bounds;double travel{};unsigned points{};std::vector<StrokeTiming> timing{};};
void checkCancelled(const std::atomic<bool>*);
Group checked(QJsonArray,QString kind="tom_handwriting");
QRectF bounds(const QJsonArray&);QJsonArray transform(QJsonArray,const QJsonArray& matrix,QString color={});
QJsonArray lasso(const QJsonArray& strokes,const QJsonArray& polygon,bool wholeGroups=true);
struct Cut {bool hit{};QJsonArray pieces;};
Cut erase(const QJsonObject& stroke,const QJsonArray& eraser,double radius,std::uint64_t& work,std::uint64_t* broadphase=nullptr);
void polygonSpans(const QJsonArray& polygon,int width,int height,const QJsonArray& matrix,const std::function<void(int,int,int)>&,const QJsonArray& holes={});
QByteArray svg(const QJsonArray& strokes,double width,double height);
}
