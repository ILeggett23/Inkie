// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "underlay.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <filesystem>
namespace ux {
// Starred notes, keyed by note id (UUIDs, unique across books). Kept beside the
// library (ui-note-favorites.json), apart from the notes store, so starring never
// touches note content or its journal. A star whose note is deleted is never shown again.
class NoteFavorites {
public:
 static constexpr int Capacity=4096;
 void load(const std::filesystem::path& file){
  file_=file;keys_.clear();
  try{auto o=QJsonDocument::fromJson(underlay::readBounded(file,512*1024)).object();
   if(o["version"].toInt()!=1)return;
   for(auto v:o["keys"].toArray())if(v.isString()&&keys_.size()<Capacity&&valid(v.toString()))keys_.insert(v.toString());}catch(...){keys_.clear();}
 }
 bool contains(const QString& note)const{return !note.isEmpty()&&keys_.contains(note);}
 // Returns false (and changes nothing) when the star cannot be saved.
 bool toggle(const QString& note){
  if(file_.empty()||!valid(note))return false;
  const auto& k=note;const bool had=keys_.contains(k);
  if(!had&&keys_.size()>=Capacity)return false;
  if(had)keys_.remove(k);else keys_.insert(k);
  QJsonArray keys;for(const auto& s:keys_)keys.append(s);
  try{underlay::atomicFile(file_,QJsonDocument(QJsonObject{{"version",1},{"keys",keys}}).toJson(QJsonDocument::Compact));return true;}
  catch(...){if(had)keys_.insert(k);else keys_.remove(k);return false;}
 }
private:
 // Note ids are UUIDs; anything else is ignored.
 static bool valid(const QString& k){return !k.isEmpty()&&k.size()<=128&&!k.contains(QLatin1Char('/'))&&!k.contains(QChar(0));}
 std::filesystem::path file_;QSet<QString> keys_;
};
}
