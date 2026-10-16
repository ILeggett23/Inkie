#pragma once
#include "library.h"
#include "core.h"
#include "ib/components.h"
namespace ux {
enum class LibraryManageCommand {None,Changed,Cancel,Rename,Folder,Trash,Restore,Delete,Favorite};
// Touch targets of the manage sheet (see LibraryManage::targets()).
enum LibraryManageId:int {ManageFavorite=1,ManageRename,ManageMove,ManageTrash,ManageRestore,ManageDelete,ManageClose,ManageConfirmDelete,ManageCancelDelete};
struct LibraryManageResult {LibraryManageCommand command{LibraryManageCommand::None};QString id,text;};
// Pure view and command producer. Storage and all contact arbitration belong to the shell.
class LibraryManage {
 enum class Page {Closed,Menu,Rename,Folder,ConfirmDelete};Page page_{Page::Closed};
 library::Document document_;QString value_;bool upper_{},symbols_{},favorite_{};ib::Targets targets_;
 LibraryManageResult result(LibraryManageCommand,bool close=false);
public:
 void open(const library::Document&);
 void setFavorite(bool v)noexcept{favorite_=v;}QString document()const{return document_.id;}
 int effectScene()const noexcept{return int(page_);}
 bool active()const{return page_!=Page::Closed;}
 void paint(spike::Surface);
 LibraryManageResult hit(int x,int y);
 const ib::Targets& targets()const noexcept{return targets_;}
};
}
