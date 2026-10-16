// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Settings > Tom and Accessibility (ui-prefs.json in the library root). Unknown or malformed fields keep their
// defaults: Tom replies in handwriting, medium size, written in line by line; normal contrast and motion.
#include "underlay.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <filesystem>
namespace ux {
struct Prefs {
 bool tomTyped{};unsigned tomHand{1};bool tomReveal{true}; // Tom replies: typed instead of handwriting; 0..2 size; line-by-line
 bool highContrast{},reduceMotion{};                      // Accessibility
 bool operator==(const Prefs&)const=default;
 static Prefs load(const std::filesystem::path& root)noexcept{
  Prefs p;try{auto o=QJsonDocument::fromJson(underlay::readBounded(root/"ui-prefs.json",4096)).object();if(o["version"]!=1)return p;
   if(o["tomTyped"].isBool())p.tomTyped=o["tomTyped"].toBool();if(o["tomHand"].isDouble()&&o["tomHand"].toInt(-1)>=0&&o["tomHand"].toInt()<=2)p.tomHand=unsigned(o["tomHand"].toInt());
   if(o["tomReveal"].isBool())p.tomReveal=o["tomReveal"].toBool();if(o["highContrast"].isBool())p.highContrast=o["highContrast"].toBool();if(o["reduceMotion"].isBool())p.reduceMotion=o["reduceMotion"].toBool();}catch(...){}
  return p;}
 void save(const std::filesystem::path& root)const{
  underlay::atomicFile(root/"ui-prefs.json",QJsonDocument(QJsonObject{{"version",1},{"tomTyped",tomTyped},{"tomHand",int(tomHand)},{"tomReveal",tomReveal},{"highContrast",highContrast},{"reduceMotion",reduceMotion}}).toJson(QJsonDocument::Compact));}
};
}
