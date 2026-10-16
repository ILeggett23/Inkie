// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "library.h"
#include "epub_view_store.h"
#include <optional>
namespace tom {
struct SourceTarget {QString document,layoutId,viewId;int page{},pages{};};
// Background/UI-boundary only. Frozen context is never rewritten to make a
// different layout's numerical page appear valid. Unsafe/missing receipts refuse.
inline std::optional<SourceTarget> resolveSource(const std::filesystem::path& libraryRoot,const QJsonObject& source)noexcept{
 try{
  const auto id=source["document"].toString(),revision=source["revision"].toString(),format=source["format"].toString();
  if(!library::validId(id)||!library::validHash(revision)||(format!="pdf"&&format!="epub")||!source["page"].isDouble())return {};
  const int page=source["page"].toInt(-1);if(page<0||page>=1000||source["page"].toDouble()!=page)return {};
  const auto root=library::documentRoot(libraryRoot,id);const auto descriptor=library::descriptor(root);
  if(descriptor.isEmpty()||descriptor["id"]!=id||descriptor["sha256"]!=revision||descriptor["format"]!=format||std::filesystem::exists(libraryRoot/"tombstones"/(id.toStdString()+".json")))return {};
  // A trashed document is not a navigable source (Shell navigation refuses it too).
  // Restore makes it available again; permanent deletion leaves a tombstone above.
  for(const auto& trashed:library::documents(libraryRoot,true))if(trashed.id==id)return {};
  SourceTarget target{id,descriptor["layout"].toString(),{},page,descriptor["pages"].toInt()};
  if(format=="epub"){
   const auto active=reader::epubviews::activeSet(root);
   if(source.contains("viewId")&&!source["viewId"].isString())return {};
   if(source["layoutId"]!=active.receipt["layout_id"]||source["viewId"].toString()!=active.id)return {};
   target.layoutId=active.receipt["layout_id"].toString();target.viewId=active.id;target.pages=active.receipt["pages"].toInt();
  }
  if(target.pages<1||target.pages>1000||page>=target.pages)return {};
  return target;
 }catch(...){return {};}
}
}
