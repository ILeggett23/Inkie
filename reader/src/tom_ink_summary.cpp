// SPDX-License-Identifier: AGPL-3.0-only
#include "tom_ink_summary.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QStringList>
#include <QHash>
#include <cmath>
namespace tomink {
QJsonObject structuredReply(QString text){if(text.size()>32768)return {};text=text.trimmed();if(text.startsWith("```")){int first=text.indexOf('\n'),last=text.lastIndexOf("```");if(first>=0&&last>first)text=text.mid(first+1,last-first-1).trimmed();}auto doc=QJsonDocument::fromJson(text.toUtf8());if(!doc.isObject())return {};auto result=doc.object();if(!QStringList{"artwork","bar_chart","line_graph","timeline","flowchart","mind_map","diagram"}.contains(result["type"].toString()))return {};return result;}
QString displayReply(const QString& text){auto data=structuredReply(text);if(data.isEmpty())return text;QStringList lines;auto title=data["title"].toString();if(!title.isEmpty())lines.append(title.left(128));const auto type=data["type"].toString();if(type=="artwork"){lines.append(data["prompt"].toString().left(1200));for(auto value:data["labels"].toArray())if(!value.toObject()["text"].toString().isEmpty())lines.append(value.toObject()["text"].toString().left(64));}else if(type=="bar_chart"||type=="line_graph"){auto values=data["values"].toArray();if(values.size()>12)return "Inkie's chart needs fewer values.";for(auto value:values){auto v=value.toObject();if(!v["value"].isDouble()||!std::isfinite(v["value"].toDouble()))return "Inkie's chart needs valid values.";lines.append(v["label"].toString().left(32)+": "+QString::number(v["value"].toDouble(),'g',17));}}else{auto nodes=data["nodes"].toArray(),edges=data["edges"].toArray();if(nodes.size()>12||edges.size()>24)return "Inkie's diagram needs fewer steps.";QHash<QString,QString> labels;for(auto value:nodes){auto node=value.toObject();auto label=node["label"].toString().left(64);labels[node["id"].toString()]=label;lines.append(label);}for(auto value:edges){auto edge=value.toObject();auto from=labels.value(edge["from"].toString()),to=labels.value(edge["to"].toString());if(!from.isEmpty()&&!to.isEmpty())lines.append(from+QStringLiteral(" → ")+to);}}return lines.join("\n");}
}
