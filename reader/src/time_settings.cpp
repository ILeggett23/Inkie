// SPDX-License-Identifier: AGPL-3.0-only
#include "time_settings.h"
#include "underlay.h"
#include <QJsonDocument>
#include <QJsonObject>
namespace clockprefs {
bool valid(const QString& id)noexcept{try{return id.isEmpty()||(id.size()<=128&&!id.contains(QChar(0))&&QTimeZone(id.toUtf8()).isValid());}catch(...){return false;}}
Setting load(const std::filesystem::path& root)noexcept{try{auto o=QJsonDocument::fromJson(underlay::readBounded(root/"settings/timezone.json",4096)).object();if(o["version"]==1&&o["zone"].isString()&&valid(o["zone"].toString()))return {o["zone"].toString()};}catch(...){}return {};}
bool save(const std::filesystem::path& root,const Setting& s)noexcept{if(!valid(s.zone))return false;try{underlay::privateDirectory(root/"settings");underlay::atomicFile(root/"settings/timezone.json",QJsonDocument(QJsonObject{{"version",1},{"zone",s.zone}}).toJson(QJsonDocument::Compact));return true;}catch(...){return false;}}
Reading at(qint64 seconds,const Setting& s){auto zone=s.zone.isEmpty()?QTimeZone::systemTimeZone():QTimeZone(s.zone.toUtf8());if(!zone.isValid())zone=QTimeZone::utc();const auto date=QDateTime::fromSecsSinceEpoch(seconds,zone);return {date.toString("HH:mm"),date.timeZoneAbbreviation()};}
QString label(const Setting& s){return s.zone.isEmpty()?QString("System (%1)").arg(QString::fromUtf8(QTimeZone::systemTimeZoneId())):s.zone;}
}
