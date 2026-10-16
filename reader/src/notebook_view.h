#pragma once
#include "core.h"
#include "text_input.h"
#include "notebook_pdf.h"
#include "ib/sheets.h"
namespace ux {
// New notebook sheet (concept #10). Pure view; the shell generates and imports.
class NotebookView {
public:
 enum class Action {None,Changed,Close,Create,Import};
 void open(QString folder);bool active()const noexcept{return active_;}void close()noexcept{active_=false;input_.cancel();input_.acknowledge();}
 void setBusy(bool busy,QString status={}){busy_=busy;status_=std::move(status);}
 void paint(spike::Surface);Action hit(int x,int y);
 QString title()const{return title_.trimmed().isEmpty()?QString("Notebook"):title_.trimmed();}QString folder()const{return folder_.trimmed();}
 notebook::Paper paper()const noexcept{return notebook::Paper(unsigned(paper_));}int pages()const noexcept{return pages_;}
 const ib::Targets& targets()const noexcept{return targets_;}
private:
 bool active_{},busy_{},editingTitle_{};ib::sheets::Template paper_{ib::sheets::Template::Lined};QString title_,folder_,status_;int pages_{8};TextInput input_;ib::Targets targets_;
};
}
