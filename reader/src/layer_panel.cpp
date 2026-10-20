// SPDX-License-Identifier: AGPL-3.0-only
#include "layer_panel.h"
#include <algorithm>
namespace notes {namespace {
std::string utf8(const QString& v){auto b=v.toUtf8();return std::string(b.constData(),std::size_t(b.size()));}
// Display order is top-most first; the stored array is bottom-most first.
int stored(const QJsonArray& rows,int display){return int(rows.size())-1-display;}
}
void Layers::paint(spike::Surface s,const View& view,ib::Targets& targets,std::string status,bool turned){
 auto rows=view.doc["layers"].toArray();if(selected_.isEmpty())selected_=view.doc["activeLayer"].toString();if(offset_>=unsigned(rows.size()))offset_=0;
 ib::Canvas c(s);ib::sheets::Layers m;m.offset=offset_;m.menuFor=menuFor_;m.confirmDelete=confirmDelete_;m.busy=view.busy;m.status=std::move(status);m.rect=ib::sheets::layersRect(turned);
 m.canUndo=!view.doc["undo"].toArray().isEmpty();m.canRedo=!view.doc["redo"].toArray().isEmpty();m.background=view.page["format"]=="epub"?"Book (EPUB)":"Document (PDF)";
 for(int d=0;d<rows.size();++d){auto layer=rows[stored(rows,d)].toObject();ib::sheets::Layer l;l.name=utf8(layer["name"].toString());l.visible=layer["visible"].toBool();l.locked=layer["locked"].toBool();
  l.active=layer["id"]==view.doc["activeLayer"];l.selected=layer["id"].toString()==selected_;
  for(auto key:{"strokes","highlights","notes"})for(auto v:view.doc[key].toArray())l.items+=v.toObject()["layer"]==layer["id"];m.layers.push_back(std::move(l));}
 ib::sheets::layers(c,targets,m);
}
QJsonObject Layers::finger(int id,const View& view){namespace S=ib::sheets;const auto rows=view.doc["layers"].toArray();
 auto layerAt=[&](int display){return display>=0&&display<rows.size()?rows[stored(rows,display)].toObject():QJsonObject{};};
 if(id==S::LayersClose){menuFor_=-1;return {{"op","ui.close"}};}
 if(id==S::LayerAdd){menuFor_=-1;return {{"op","ui.layer.add"}};}
 if(id==S::LayersUndo||id==S::LayersRedo){menuFor_=-1;return {{"op",id==S::LayersUndo?"undo":"redo"}};}
 if(id==S::LayersPrev){offset_=offset_>=S::LayerPageSize?offset_-S::LayerPageSize:0;menuFor_=-1;return {};}
 if(id==S::LayersNext){if(offset_+S::LayerPageSize<unsigned(rows.size()))offset_+=S::LayerPageSize;menuFor_=-1;return {};}
 if(id>=S::EyeBase&&id<S::EyeBase+100){auto layer=layerAt(id-S::EyeBase);menuFor_=-1;if(layer.isEmpty())return {};return {{"op","layer.visible"},{"layer",layer["id"]},{"value",!layer["visible"].toBool()}};}
 if(id>=S::RowMenuBase&&id<S::RowMenuBase+100){int d=id-S::RowMenuBase;auto layer=layerAt(d);if(layer.isEmpty())return {};select(layer["id"].toString());menuFor_=menuFor_==d?-1:d;return {};}
 if(id>=S::RowBase&&id<S::RowBase+100){auto layer=layerAt(id-S::RowBase);menuFor_=-1;if(layer.isEmpty())return {};select(layer["id"].toString());
  if(layer["locked"].toBool()||!layer["visible"].toBool()||layer["id"]==view.doc["activeLayer"])return {};return {{"op","layer.select"},{"layer",layer["id"]}};}
 if(menuFor_>=0&&id>=S::MenuRename&&id<=S::MenuActivate){auto layer=layerAt(menuFor_);int display=menuFor_;if(layer.isEmpty()){menuFor_=-1;return {};}
  auto key=layer["id"];int index=stored(rows,display);
  switch(id){
  case S::MenuActivate:menuFor_=-1;return {{"op","layer.select"},{"layer",key}};
  case S::MenuRename:menuFor_=-1;return {{"op","ui.layer.rename"},{"layer",key},{"name",layer["name"]}};
  case S::MenuLock:menuFor_=-1;return {{"op","layer.locked"},{"layer",key},{"value",!layer["locked"].toBool()}};
  case S::MenuUp:menuFor_=-1;if(index+1<rows.size())return {{"op","layer.reorder"},{"layer",key},{"index",index+1}};return {};
  case S::MenuDown:menuFor_=-1;if(index>0)return {{"op","layer.reorder"},{"layer",key},{"index",index-1}};return {};
  case S::MenuDelete:{bool nonempty=false;for(auto k:{"strokes","highlights","notes"})for(auto v:view.doc[k].toArray())nonempty|=v.toObject()["layer"]==key;
   if(nonempty&&!confirmDelete_){confirmDelete_=true;return {};}confirmDelete_=false;menuFor_=-1;return {{"op","layer.delete"},{"layer",key},{"confirmed",true}};}
  default:break;}}
 if(menuFor_>=0){menuFor_=-1;confirmDelete_=false;}
 return {};
}
}
