#include "semantic_controller.h"
#include "semantic_range.h"
namespace semantic {
bool Controller::select(Selection s,bool append){if(!s)return false;auto next=active_?mergeSelection(active_->geometry,selected_,std::move(s),generation_+1,append):Selection{};if(!next)return false;++generation_;selected_=std::move(next);definitionOnly_=false;definitionExpanded_=false;definitionOffset_=0;definition_={};notice_.clear();view_=View::Menu;dirty_=true;return true;}
void Controller::cancel(){definitionOnly_=false;definitionExpanded_=false;definitionOffset_=0;selected_.reset();definition_={};notice_.clear();view_=View::None;++generation_;dirty_=true;}
void Controller::bind(underlay::Prepared& p){cancel();loading_=false;active_.reset();if(!p.text)return;if(p.semanticPage)active_=p.semanticPage;
 if(!active_)for(auto& w:registry_)if(auto page=w.lock();page&&page->geometry&&page->geometry->pageId()==p.text->pageId()){active_=page;break;}
 if(!active_){active_=std::make_shared<PageState>();active_->geometry=p.text;active_->loaded=!p.text->available();bool slot=false;for(auto& w:registry_)if(w.expired()){w=active_;slot=true;break;}if(!slot){active_->loaded=true;active_->error=true;active_->errorText="SEMANTIC CACHE FULL";failed_=true;}}
 p.semanticPage=active_;
}
void Controller::merge(const std::shared_ptr<PageState>& page,QJsonArray records){for(auto& pending:pendingHighlights_)if(pending&&pending->pageId()==page->geometry->pageId()){bool found=false;for(auto r:records)found|=r.toObject()["id"].toString()==pending->id();if(!found)records.append(pending->data);}page->highlights=records;}
void Controller::tick(){Result r;while(worker_.poll(r)){if(r.error){failed_=true;r.page->error=true;r.page->loaded=true;r.page->errorText=r.message;if(r.page==active_){notice_="SEMANTIC DATA UNAVAILABLE";view_=View::Notice;dirty_=true;}continue;}
 if(r.kind==Result::Load){r.page->loaded=true;merge(r.page,r.data["records"].toArray());}
 if(r.kind==Result::Highlight){++highlightAcks_;for(auto& pending:pendingHighlights_)if(pending&&pending->id()==r.selection->id())pending.reset();merge(r.page,r.data["records"].toArray());}
 if(r.kind==Result::Define){if(r.data["status"].toString()=="FOUND")++vocabularyAcks_;if(selected_&&r.selection->data["generation"]==selected_->data["generation"]&&r.selection->pageId()==selected_->pageId()){definition_=r.data;view_=View::Definition;dirty_=true;}}
 if(r.page==active_)dirty_=true;
 }
 if(active_&&!active_->loaded&&!loading_){if(worker_.failed()){active_->loaded=true;active_->error=true;failed_=true;}else loading_=worker_.request(Result::Load,active_);}
}
bool Controller::longPress(double x,double y,const QString& id){probe(Work::Layout);if(!active_||!active_->geometry||!active_->geometry->available()){cancel();notice_=active_&&active_->geometry?active_->geometry->data["status"].toString():"TEXT UNAVAILABLE";view_=View::Notice;dirty_=true;return false;}if(active_->geometry->pageId()!=id)return false;if(active_->error){cancel();notice_="SEMANTIC DATA UNAVAILABLE";view_=View::Notice;dirty_=true;return false;}
 auto s=hit(active_->geometry,x,y,generation_+1);if(!s)return false;return select(std::move(s));}
bool Controller::defineAt(double x,double y,const QString& id){
 if(!active_||!active_->geometry||active_->geometry->pageId()!=id)return false;
 if(active_->error||!active_->geometry->available()){notify("Dictionary text unavailable on this page");return false;}
 auto selected=definitionHit(active_->geometry,x,y,generation_+1);if(!selected)return false;
 ++generation_;selected_=std::move(selected);definitionOnly_=true;definitionExpanded_=false;definitionOffset_=0;definition_={};notice_.clear();view_=View::None;
 if(action(Action::Define))return true;
 notice_="No definition available for this text";view_=View::Notice;dirty_=true;return true;
}
bool Controller::action(Action a){if(!selected_||!active_||selected_->pageId()!=active_->geometry->pageId()||active_->error||support(a,*selected_)!=ActionSupport::Ready)return false;
 if(a==Action::Highlight){for(auto r:active_->highlights)if(r.toObject()["id"].toString()==selected_->id()){dismiss();return true;}if(active_->highlights.size()>=MaxHighlights){notice_="HIGHLIGHT CAPACITY REACHED";view_=View::Notice;dirty_=true;return false;}Selection* slot=nullptr;for(auto& p:pendingHighlights_)if(!p){slot=&p;break;}if(!slot||!worker_.request(Result::Highlight,active_,selected_))return false;*slot=selected_;active_->highlights.append(selected_->data);dismiss();return true;}
 if(a==Action::Define&&view_!=View::Defining){if(!worker_.request(Result::Define,active_,selected_))return false;view_=View::Defining;dirty_=true;return true;}return false;
}
void Controller::dismiss(){cancel();}
}
