// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "underlay.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>
#include <optional>
#include <vector>
namespace ux {
// Page bookmarks, kept beside the library (ui-bookmarks.json), apart from the document
// stores. A mark remembers the page, the page count and the layout it was made in:
// in that layout (always, for PDF) it is exact; after an EPUB reflow it maps by
// proportion and is shown as approximate.
class Bookmarks {
public:
 static constexpr int PerBook=64,Books=512;
 struct Mark {int page{},pages{1};QString layout,source,viewSet;};
 struct Place {int page{};bool approximate{};int mark{-1};};
 void load(const std::filesystem::path& file){
  file_=file;books_.clear();
  try{auto o=QJsonDocument::fromJson(underlay::readBounded(file,1024*1024)).object();if(o["version"].toInt()!=1)return;
   auto books=o["books"].toObject();for(auto i=books.begin();i!=books.end()&&books_.size()<Books;++i){if(!validText(i.key()))continue;QVector<Mark> marks;
    for(auto v:i.value().toArray()){auto m=v.toObject();Mark k{m["page"].toInt(-1),m["pages"].toInt(0),m["layout"].toString(),{}, {}};if(hashText(m["source"].toString())&&m["view"].isString()&&(m["view"].toString().isEmpty()||hashText(m["view"].toString()))){k.source=m["source"].toString();k.viewSet=m["view"].toString();}if(k.page>=0&&k.pages>k.page&&k.pages<=100000&&validText(k.layout)&&marks.size()<PerBook)marks.push_back(k);}
    if(!marks.isEmpty())books_.insert(i.key(),marks);}}catch(...){books_.clear();}
 }
 bool has(const QString& book,const QString& layout,int page)const{for(const auto& m:books_.value(book))if(m.layout==layout&&m.page==page)return true;return false;}
 // Returns false (and changes nothing) when the mark cannot be saved.
 std::optional<Mark> mark(const QString& book,int index)const{auto found=books_.constFind(book);if(found==books_.cend()||index<0||index>=found->size())return {};return (*found)[index];}
 bool toggle(const QString& book,const QString& layout,int page,int pages,const QString& source={},const QString& viewSet={}){
  if(file_.empty()||!validText(book)||!validText(layout)||page<0||pages<=page||pages>100000||(!source.isEmpty()&&!hashText(source))||(!viewSet.isEmpty()&&(!hashText(viewSet)||source.isEmpty())))return false;
  auto before=books_;auto& marks=books_[book];
  auto at=std::find_if(marks.begin(),marks.end(),[&](const Mark& m){return m.layout==layout&&m.page==page;});
  if(at!=marks.end())marks.erase(at);else{if(marks.size()>=PerBook||(marks.isEmpty()&&books_.size()>Books)){books_=before;return false;}marks.push_back({page,pages,layout,source,viewSet});}
  if(marks.isEmpty())books_.remove(book);
  if(!save()){books_=before;return false;}return true;
 }
 // Where each mark lands in the current layout, in page order, without duplicates.
 std::vector<Place> places(const QString& book,const QString& layout,int pages)const{
  std::vector<Place> out;if(pages<1)return out;
  int index=0;for(const auto& m:books_.value(book)){Place p;if(m.layout==layout&&m.page<pages)p={m.page,false,index};
   else{double ratio=m.pages>1?double(m.page)/double(m.pages-1):0.0;p={int(std::lround(ratio*double(pages-1))),true,index};}
   ++index;p.page=std::clamp(p.page,0,pages-1);auto same=std::find_if(out.begin(),out.end(),[&](const Place& q){return q.page==p.page;});
   if(same==out.end())out.push_back(p);else if(same->approximate&&!p.approximate)*same=p;}
  std::sort(out.begin(),out.end(),[](const Place& a,const Place& b){return a.page<b.page;});return out;
 }
private:
 static bool hashText(const QString& s){if(s.size()!=64)return false;for(auto c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
 static bool validText(const QString& s){return !s.isEmpty()&&s.size()<=256&&!s.contains(QChar(0));}
 bool save()const{QJsonObject books;for(auto i=books_.begin();i!=books_.end();++i){QJsonArray marks;for(const auto& m:i.value()){QJsonObject o{{"page",m.page},{"pages",m.pages},{"layout",m.layout}};if(!m.source.isEmpty()){o["source"]=m.source;o["view"]=m.viewSet;}marks.append(o);}books.insert(i.key(),marks);}
  try{auto bytes=QJsonDocument(QJsonObject{{"version",1},{"books",books}}).toJson(QJsonDocument::Compact);if(bytes.size()>1024*1024)return false;underlay::atomicFile(file_,bytes);return true;}catch(...){return false;}}
 std::filesystem::path file_;QHash<QString,QVector<Mark>> books_;
};
}
