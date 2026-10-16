#include "notebook_view.h"
namespace ux {
namespace {std::string utf8(const QString& v){auto b=v.toUtf8();return std::string(b.constData(),std::size_t(b.size()));}}
void NotebookView::open(QString folder){active_=true;busy_=false;status_.clear();title_.clear();folder_=std::move(folder);pages_=8;paper_=ib::sheets::Template::Lined;input_.cancel();input_.acknowledge();}
void NotebookView::paint(spike::Surface s){if(!active_)return;ib::Canvas c(s);targets_.clear();
 ib::sheets::NewNotebook m;m.paper=paper_;m.title=utf8(title_);m.folder=utf8(folder_);m.pages=pages_;m.busy=busy_;m.status=utf8(status_);ib::sheets::newNotebook(c,targets_,m);
 if(input_.active())input_.paint(s);}
NotebookView::Action NotebookView::hit(int x,int y){if(!active_)return Action::None;
 if(input_.active()){input_.finger(x,y);if(input_.done()){(editingTitle_?title_:folder_)=input_.text().left(editingTitle_?120:80);input_.acknowledge();}else if(input_.cancelled())input_.acknowledge();return Action::Changed;}
 if(busy_)return Action::None;namespace S=ib::sheets;int id=targets_.find(x,y);
 if(id<0&&!S::NewNotebookRect.contains(x,y)){close();return Action::Close;}
 switch(id){
 case S::NewClose:close();return Action::Close;
 case S::NewCreate:return Action::Create;
 case S::NewImport:return Action::Import;
 case S::NewTitle:editingTitle_=true;input_.open(title_,"Notebook title",120);return Action::Changed;
 case S::NewFolder:editingTitle_=false;input_.open(folder_,"Folder (empty for My files)",80);return Action::Changed;
 case S::NewPagesLess:pages_=std::max(1,pages_-1);return Action::Changed;
 case S::NewPagesMore:pages_=std::min(64,pages_+1);return Action::Changed;
 default:if(id>=S::TemplateBase&&id<S::TemplateBase+4){paper_=S::Template(unsigned(id-S::TemplateBase));return Action::Changed;}return Action::None;}
}
}
