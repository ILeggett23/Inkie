#include "library_manage.h"
#include "ib/tom_screen.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using C=ux::LibraryManageCommand;
void check(bool b,const char* what){if(!b)throw std::runtime_error(std::string("library manage test failure: ")+what);}
// The sheet records its targets while painting (as on device): paint, then tap.
static std::vector<std::uint32_t> pixels(1632*2160);
static ux::LibraryManageResult tap(ux::LibraryManage& v,int id){v.paint({pixels,1620,2160,1632});auto r=v.targets().rect(id);if(r.empty())return {};return v.hit((r.x0+r.x1)/2,(r.y0+r.y1)/2);}
int main(){try{library::Document d;d.id="stable-id";d.title="Original";d.folder="Existing";ux::LibraryManage view;check(!view.active(),"closed");check(view.hit(100,550).command==C::None,"closed_no_command");
 view.open(d);check(view.active(),"opens");auto r=tap(view,ux::ManageRename);check(r.command==C::Changed&&r.id==d.id&&r.text==d.title,"rename_prefills_title");
 tap(view,ib::KeyBackspace);r=tap(view,ib::KeyDone);check(r.command==C::Rename&&r.id==d.id&&r.text=="Origina"&&!view.active(),"rename_backspace_save");
 view.open(d);tap(view,ux::ManageMove);r=tap(view,ib::KeyDone);check(r.command==C::Folder&&r.text==d.folder&&r.id==d.id,"folder_keeps_existing");
 view.open(d);tap(view,ux::ManageRename);tap(view,ib::KeyClear);tap(view,ib::KeyBase+'q');tap(view,ib::KeySpace);tap(view,ib::KeySymbols);tap(view,ib::KeyBase+'1');r=tap(view,ib::KeyDone);check(r.command==C::Rename&&r.text=="q 1","typed_rename");
 view.open(d);tap(view,ux::ManageRename);tap(view,ib::KeyClear);check(tap(view,ib::KeyDone).command==C::None&&view.active(),"empty_title_refused");r=tap(view,ib::KeyCancel);check(r.command==C::Cancel&&r.id==d.id&&!view.active(),"cancel_closes");
 view.open(d);r=tap(view,ux::ManageTrash);check(r.command==C::Trash&&r.id==d.id,"move_to_trash");
 view.open(d);r=tap(view,ux::ManageFavorite);check(r.command==C::Favorite&&r.id==d.id&&view.active(),"favorite_toggle_keeps_sheet");
 d.trashed=true;view.open(d);check(tap(view,ux::ManageRename).command==C::None,"trashed_has_no_rename");r=tap(view,ux::ManageRestore);check(r.command==C::Restore&&r.id==d.id,"restore");
 view.open(d);r=tap(view,ux::ManageDelete);check(r.command==C::Changed&&view.active(),"delete_requires_confirmation");r=tap(view,ux::ManageCancelDelete);check(r.command==C::Cancel&&!view.active(),"delete_cancel");
 view.open(d);tap(view,ux::ManageDelete);r=tap(view,ux::ManageConfirmDelete);check(r.command==C::Delete&&r.id==d.id&&!view.active(),"confirmed_delete");
 d.trashed=false;d.title=QString(180,'a');view.open(d);tap(view,ux::ManageRename);check(tap(view,ib::KeyBase+'b').command==C::Changed,"at_limit_key_ignored");r=tap(view,ib::KeyDone);check(r.text.size()==180,"bounded_title");
 d.title=QString::fromUtf8("A😀");view.open(d);tap(view,ux::ManageRename);tap(view,ib::KeyBackspace);r=tap(view,ib::KeyDone);check(r.text=="A","surrogate_pair_backspace");
 view.open(d);view.paint({pixels,1620,2160,1632});std::cout<<"library manage tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
