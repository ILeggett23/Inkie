#include "epub_annotation_reflow.h"
#include "library.h"
#include "semantic_range.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace reader::epubannotations { namespace {
QJsonObject unresolved(const QString& reason){return {{"status","unresolved"},{"reason",reason}};}
bool integer(const QJsonValue& value,int minimum,int maximum){return value.isDouble()&&std::isfinite(value.toDouble())&&value.toDouble()>=minimum&&value.toDouble()<=maximum&&std::floor(value.toDouble())==value.toDouble();}
bool matches(const epub::ReflowDocument& doc,const epubviews::ViewSet& view){
 const auto r=view.receipt;
 if(r["format"]!="epub"||r["source_sha"]!=doc.sourceSha256()||r["pages"]!=doc.pageCount()||r["layout"].toObject()!=doc.layoutDescriptor())return false;
 if(!view.id.isEmpty())return r["layout_id"]==doc.layoutId();
 const auto l=doc.layoutDescriptor();return l["width"]==600&&l["height"]==800&&l["fontSize"]==12&&r["layout_id"]==r["base_layout"];
}
epub::Layout parameters(const epubviews::ViewSet& s){auto l=s.receipt["layout"].toObject();return {l["width"].toInt(),l["height"].toInt(),l["fontSize"].toInt(),l["userCss"].toString()};}
QJsonArray selectedQuads(const QJsonObject& page,int first,int last){QJsonArray out;for(auto value:page["chars"].toArray()){const auto c=value.toObject();const int at=c["offset"].toInt(),length=c["length"].toInt();if(at>=first&&at+length<=last)out.append(c["quad"]);}return out;}
}
QString anchorDigest(const QJsonObject& original){return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(original).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());}
QJsonObject capture(epub::ReflowDocument& doc,const epubviews::ViewSet& view,const QJsonObject& a){
 if(!matches(doc,view))return unresolved("epub-annotation-original-layout-mismatch");
 const auto r=view.receipt;
 if(a["format"]!="epub"||a["document"]!=r["document"]||a["revision"]!=r["source_sha"]||a["layoutId"]!=r["layout_id"]||!integer(a["page"],0,doc.pageCount()-1))return unresolved("epub-annotation-original-identity");
 if(a.contains("viewId")&&a["viewId"]!=view.id)return unresolved("epub-annotation-original-view-mismatch");
 if(!semantic::validAnchorSegments(a))return unresolved("epub-annotation-segments-invalid");if(a.contains("segments")){QJsonArray anchors,quads;const auto page=doc.page(a["page"].toInt());for(auto v:a["segments"].toArray()){auto part=a;part.remove("segments");auto segment=v.toObject();for(auto it=segment.begin();it!=segment.end();++it)part[it.key()]=it.value();auto selected=selectedQuads(page,part["start"].toInt(),part["end"].toInt());for(auto q:selected)quads.append(q);part["quads"]=selected;auto found=capture(doc,view,part);if(found["status"]!="anchored")return found;anchors.append(found["anchor"]);}if(quads!=a["quads"].toArray())return unresolved("epub-annotation-segment-quads-mismatch");return {{"status","anchored"},{"anchors",anchors},{"originalAnchorDigest",anchorDigest(a)},{"originalViewId",view.id}};}
 const int pageIndex=a["page"].toInt();const auto page=doc.page(pageIndex);
 // Older baseline geometry may have only a numeric spine label. The immutable
 // receipt plus exact page/range/text/quads below provide the source binding;
 // structured OPF identities, when present, must agree with actual extraction.
 const auto location=a["publicationLocation"].toObject();
 for(auto key:{"chapter","chapterPage","spineItem"})if(location.contains(key)&&location[key]!=page[key])return unresolved("epub-annotation-original-publication-location");
 if(a["spineItem"].isString()&&a["spineItem"].toString().contains('/')&&a["spineItem"]!=page["spineItem"])return unresolved("epub-annotation-original-spine");
 QJsonObject result;
 if(a["scope"]=="page-location"&&a["excerpt"].toString().isEmpty()&&!a.contains("start")&&!a.contains("end")&&a["quads"].toArray().isEmpty()){
  result=doc.captureReadingPosition(pageIndex);
  if(result["status"]!="anchored")result=doc.captureSpineStart(pageIndex);
 }else{
  if(!integer(a["start"],0,32768)||!integer(a["end"],1,32768)||!a["excerpt"].isString()||!a["quads"].isArray())return unresolved("epub-annotation-selection-evidence-missing");
  const int first=a["start"].toInt(),last=a["end"].toInt();const auto text=page["text"].toString();
  if(first>=last||last>text.size()||text.mid(first,last-first)!=a["excerpt"].toString())return unresolved("epub-annotation-selection-text-mismatch");
  if(a.contains("prefix")&&a["prefix"].toString()!=text.mid(std::max(0,first-48),std::min(first,48)))return unresolved("epub-annotation-selection-prefix-mismatch");
  if(a.contains("suffix")&&a["suffix"].toString()!=text.mid(last,48))return unresolved("epub-annotation-selection-suffix-mismatch");
  const auto quads=selectedQuads(page,first,last);if(quads.isEmpty()||quads!=a["quads"].toArray())return unresolved("epub-annotation-selection-quads-mismatch");
  result=doc.captureSelection(pageIndex,first,last,a["excerpt"].toString());
 }
 if(result["status"]=="anchored"){
  // Resolution validates the immutable ReflowDocument's own document identity.
  if(result["anchor"].toObject()["document"]!=a["document"])return unresolved("epub-annotation-document-mismatch");
  result["originalAnchorDigest"]=anchorDigest(a);result["originalViewId"]=view.id;
 }
 return result;
}
QJsonObject resolve(epub::ReflowDocument& doc,const epubviews::ViewSet& view,const QJsonObject& captured){
 if(captured["status"]!="anchored")return unresolved(captured["reason"].toString("epub-annotation-not-anchored"));
 if(!matches(doc,view))return unresolved("epub-annotation-target-layout-mismatch");
 if(captured.contains("anchors")){auto anchors=captured["anchors"].toArray();if(anchors.size()<2||anchors.size()>int(semantic::MaxSelectionRanges))return unresolved("epub-annotation-segment-capacity");std::vector<QJsonObject> fragments;for(auto anchor:anchors){auto found=resolve(doc,view,QJsonObject{{"status","anchored"},{"anchor",anchor}});if(found["status"]!="resolved")return found;for(auto f:found["fragments"].toArray())fragments.push_back(f.toObject());}if(fragments.empty()||fragments.size()>1000)return unresolved("epub-annotation-segment-fragments");std::sort(fragments.begin(),fragments.end(),[](const auto& a,const auto& b){return a["page"].toInt()!=b["page"].toInt()?a["page"].toInt()<b["page"].toInt():a["first"].toInt()<b["first"].toInt();});QJsonArray all;int page=-1,last=-1,glyphs=0,characters=0;for(const auto& f:fragments){glyphs+=f["quads"].toArray().size();characters+=f["text"].toString().size();if(glyphs>int(semantic::MaxScreenRangeGlyphs)||characters>int(semantic::MaxScreenRangeUtf16))return unresolved("epub-annotation-segment-resolution-capacity");if(f["page"].toInt()==page&&f["first"].toInt()<last)return unresolved("epub-annotation-segment-overlap");page=f["page"].toInt();last=f["last"].toInt();all.append(f);}return {{"status","resolved"},{"page",all.first().toObject()["page"]},{"fragments",all},{"layoutId",view.receipt["layout_id"]},{"viewId",view.id},{"originalAnchorDigest",captured["originalAnchorDigest"]},{"sourceAnchors",anchors}};}
 const auto anchor=captured["anchor"].toObject();if(anchor["document"]!=view.receipt["document"]||anchor["sourceSha256"]!=view.receipt["source_sha"])return unresolved("epub-annotation-target-source-mismatch");
 auto result=doc.resolve(anchor);if(result["status"]=="resolved"){result["layoutId"]=view.receipt["layout_id"];result["viewId"]=view.id;result["originalAnchorDigest"]=captured["originalAnchorDigest"];result["sourceAnchor"]=anchor;}return result;
}
QJsonObject derive(const std::filesystem::path& root,const QJsonObject& annotation,const epubviews::ViewSet& target){
 const auto checked=epubviews::readSet(root,target.id);if(checked.receipt!=target.receipt)throw std::runtime_error("epub-annotation-target-receipt-changed");
 const auto r=checked.receipt;if(annotation["id"]!=r["document"]||annotation["revision"]!=r["source_sha"]||annotation["format"]!="epub")throw std::runtime_error("epub-annotation-document-identity");
 auto highlights=annotation["highlights"].toArray(),notes=annotation["notes"].toArray();if(highlights.size()>1024||notes.size()>512)throw std::runtime_error("epub-annotation-capacity");
 auto descriptor=library::descriptor(root);auto source=library::sourcePath(root,descriptor);
 epub::ReflowDocument next(source,r["document"].toString(),r["source_sha"].toString(),parameters(checked));if(!matches(next,checked))throw std::runtime_error("epub-annotation-target-layout-mismatch");
 QJsonObject cache{{"version",1},{"document",r["document"]},{"sourceSha256",r["source_sha"]},{"layoutId",r["layout_id"]},{"viewId",target.id}};
 struct Item {QString kind;int index;QJsonObject original;};QMap<QString,QList<Item>> groups;
 const auto baseline=epubviews::readSet(root,{});const auto layouts=annotation["layouts"].toObject();
 for(auto kind:{"highlights","notes"}){
  QJsonArray entries;for(auto value:annotation[kind].toArray()){
   const auto row=value.toObject(),original=row["anchor"].toObject();auto entry=unresolved("epub-annotation-origin-layout-unregistered");entry["id"]=row["id"];entry["originalAnchor"]=original;entry["originalAnchorDigest"]=anchorDigest(original);int index=int(entries.size());entries.append(entry);
   const auto layout=original["layoutId"].toString();QString id;
   if(layout==baseline.receipt["layout_id"].toString())groups[QString{}].append({kind,index,original});
   else if(layouts[layout].isObject()){
    id=layouts[layout].toObject()["view_id"].toString();if(!id.isEmpty()&&library::validHash(id))groups[id].append({kind,index,original});
   }
  }cache[kind]=entries;
 }
 // One origin layout at a time bounds immutable source/chapter working memory.
 for(auto group=groups.cbegin();group!=groups.cend();++group){
  auto publish=[&](const Item& item,QJsonObject resolved){auto entries=cache[item.kind].toArray();auto original=entries[item.index].toObject();if(resolved["status"]=="resolved")original.remove("reason");for(auto i=resolved.begin();i!=resolved.end();++i)original[i.key()]=i.value();entries[item.index]=original;cache[item.kind]=entries;};
  try{const auto origin=epubviews::readSet(root,group.key());epub::ReflowDocument old(source,r["document"].toString(),r["source_sha"].toString(),parameters(origin));
   for(const auto& item:group.value())try{auto captured=capture(old,origin,item.original);auto result=resolve(next,checked,captured);if(captured["status"]=="anchored")result["sourceAnchor"]=captured["anchor"];publish(item,result);}catch(const std::exception& e){publish(item,unresolved(QString::fromUtf8(e.what()).left(256)));}
  }catch(const std::exception& e){for(const auto& item:group.value())publish(item,unresolved(QString::fromUtf8(e.what()).left(256)));}
 }
 return cache;
}
}
