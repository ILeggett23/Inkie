/* SPDX-License-Identifier: AGPL-3.0-only */
#include "visual_references.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <stdexcept>
namespace companion {namespace {
void need(bool b){if(!b)throw std::runtime_error("visual-reference-mismatch");}
const QJsonArray& catalog(){static const auto result=QJsonDocument::fromJson(QByteArray(
#include "visual_reference_data.inc"
)).array();return result;}
QString string(QJsonValue v){return v.isString()?v.toString():QString::number(v.toDouble(),'g',17);}
QJsonObject reference(QString id){for(auto value:catalog())if(value.toObject()["id"]==id)return value.toObject();return {};}
}
void validateVisualReferences(const QJsonObject&scene){for(auto value:scene["datasets"].toArray()){auto dataset=value.toObject();auto source=dataset["source"].toString();if(!source.startsWith("reference:"))continue;auto ref=reference(source.mid(10));need(!ref.isEmpty());auto rows=dataset["rows"].toArray(),facts=ref["records"].toArray();need(rows.size()==facts.size());auto bindings=ref["bindings"].toObject(),matrix=ref["matrix"].toObject();QMap<QString,QJsonObject> records;for(auto fact:facts)records[string(fact.toObject()[bindings["id"].toString()])]=fact.toObject();QSet<QString> ids;QMap<double,std::vector<QJsonObject>> bands;int lastMain=-1;for(auto value:rows){auto row=value.toObject();auto id=row["id"].toString();need(records.contains(id)&&!ids.contains(id));ids.insert(id);auto fact=records[id];for(auto key:bindings.keys()){auto field=fact[bindings[key].toString()];need(key=="id"?row[key].toString()==string(field):row[key]==field);}if(!matrix.isEmpty()){auto column=fact[matrix["column"].toString()],logicalRow=fact[matrix["row"].toString()];need(logicalRow.isDouble());if(!column.isNull()){need(column.isDouble()&&row["row"].toDouble()==logicalRow.toDouble()-matrix["origin"].toInt()&&row["column"].toDouble()==column.toDouble()-matrix["origin"].toInt());lastMain=std::max(lastMain,row["row"].toInt());}else{row["referenceOrder"]=fact[matrix["order"].toString()];bands[logicalRow.toDouble()].push_back(row);}}}int previousBand=lastMain;for(auto it=bands.begin();it!=bands.end();++it){auto band=it.value();std::sort(band.begin(),band.end(),[](auto a,auto b){return a["referenceOrder"].toDouble()<b["referenceOrder"].toDouble();});int y=band.front()["row"].toInt(),x=band.front()["column"].toInt();need(y>previousBand);for(auto row:band){need(row["row"].toInt()==y&&row["column"].toInt()==x++);}previousBand=y;}}}
void validateVisualQuestion(const QJsonObject&scene,const QString& question){validateVisualReferences(scene);auto own=question.toLower();for(auto value:catalog()){auto ref=value.toObject();bool required=false;for(auto alias:ref["aliases"].toArray())required|=own.contains(alias.toString());if(!required)continue;bool found=false;for(auto value:scene["datasets"].toArray()){auto d=value.toObject();if(d["source"].toString()!="reference:"+ref["id"].toString())continue;for(auto value:scene["objects"].toArray()){auto o=value.toObject();found|=o["kind"]=="grid"&&o["content"].toObject()["data"]==d["id"];}}need(found);}}
}
