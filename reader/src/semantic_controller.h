#pragma once
#include "semantic.h"
#include <algorithm>
namespace semantic {
enum class View {None,Menu,Defining,Definition,Notice};
class Controller {
 Worker worker_;std::shared_ptr<PageState> active_;std::array<std::weak_ptr<PageState>,20> registry_{};
 std::array<Selection,QueueCapacity> pendingHighlights_{};Selection selected_;View view_{View::None};QJsonObject definition_;QString notice_;std::uint64_t generation_{};
 bool dirty_{},loading_{},failed_{},definitionOnly_{},definitionExpanded_{};unsigned definitionOffset_{};unsigned highlightAcks_{},vocabularyAcks_{};
 void merge(const std::shared_ptr<PageState>&,QJsonArray);
public:
 Controller(const std::filesystem::path& app,const std::filesystem::path& data):worker_(app,data){}
 void notify(QString message){cancel();notice_=std::move(message);view_=View::Notice;dirty_=true;}
 // Range selection made by drag (select mode): show the selection toolbar for it.
 bool select(Selection,bool append=true);
 void retain()noexcept{if(selected_&&!definitionOnly_){definitionExpanded_=false;definitionOffset_=0;definition_={};notice_.clear();view_=View::Menu;dirty_=true;}}
 void bind(underlay::Prepared&);void cancel();void tick();bool ready()const{return !active_||active_->loaded;}
 bool longPress(double,double,const QString& currentPageId);bool action(Action);void dismiss();
 bool defineAt(double,double,const QString& currentPageId);bool definitionOnly()const noexcept{return definitionOnly_;}
 bool definitionExpanded()const noexcept{return definitionExpanded_;}unsigned definitionOffset()const noexcept{return definitionOffset_;}
 bool expandDefinition(){if(view_!=View::Definition)return false;definitionExpanded_=!definitionExpanded_;definitionOffset_=0;dirty_=true;return true;}
 bool scrollDefinition(int delta){if(view_!=View::Definition||!definitionExpanded_)return false;definitionOffset_=unsigned(std::clamp(int(definitionOffset_)+delta,0,1024));dirty_=true;return true;}
 bool hasSelection()const{return bool(selected_);}View view()const{return view_;}Selection selection()const{return selected_;}
 QJsonObject definition()const{return definition_;}QString notice()const{return notice_;}
 Geometry geometry()const{return active_?active_->geometry:Geometry{};}
 const QJsonArray& highlights()const{static const QJsonArray empty;return active_?active_->highlights:empty;}
 bool takeDirty(){bool d=dirty_;dirty_=false;return d;}bool finish(unsigned ms){worker_.beginQuiesce();tick();bool ok=worker_.finish(ms);tick();return ok;}bool failed()const{return failed_||worker_.failed();}
 bool drained()const{return worker_.drained();}
 unsigned pending()const{return worker_.pending();}bool busy()const{return worker_.busy();}unsigned highlightAcks()const{return highlightAcks_;}unsigned vocabularyAcks()const{return vocabularyAcks_;}
};
}
