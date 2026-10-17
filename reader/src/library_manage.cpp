#include "library_manage.h"
#include "ux_paint.h"
#include "text_input.h"
#include "ib/sheets.h"
#include "ib/tom_screen.h"
namespace ux {namespace {
namespace t=ib::theme;
enum Id:int {Favorite=ManageFavorite,Rename=ManageRename,Move=ManageMove,Trash=ManageTrash,Restore=ManageRestore,Delete=ManageDelete,Close=ManageClose,ConfirmDelete=ManageConfirmDelete,CancelDelete=ManageCancelDelete};
std::string utf8(const QString& v){auto b=v.toUtf8();return std::string(b.constData(),std::size_t(b.size()));}
}
void LibraryManage::open(const library::Document& d){document_=d;value_.clear();upper_=symbols_=false;page_=Page::Menu;}
LibraryManageResult LibraryManage::result(LibraryManageCommand c,bool close){LibraryManageResult r{c,document_.id,value_};if(close)page_=Page::Closed;return r;}
LibraryManageResult LibraryManage::hit(int x,int y){using C=LibraryManageCommand;if(!active())return {};int id=targets_.find(x,y);
 if(page_==Page::Menu){switch(id){
  case Favorite:return result(C::Favorite);
  case Rename:value_=document_.title;page_=Page::Rename;return result(C::Changed);
  case Move:value_=document_.folder;page_=Page::Folder;return result(C::Changed);
  case Trash:return result(C::Trash,true);
  case Restore:return result(C::Restore,true);
  case Delete:page_=Page::ConfirmDelete;return result(C::Changed);
  case Close:return result(C::Cancel,true);
  default:return id<0?result(C::Cancel,true):LibraryManageResult{};}}
 if(page_==Page::ConfirmDelete){if(id==ConfirmDelete)return result(C::Delete,true);if(id==CancelDelete||id<0)return result(C::Cancel,true);return {};}
 if(id>=ib::KeyBase&&id<ib::KeyBase+128){if(value_.size()<180)value_+=QChar(char16_t(ib::keyCharacter(id,upper_)));upper_=false;return result(C::Changed);}
 switch(id){
 case ib::KeyShift:upper_=!upper_;return result(C::Changed);case ib::KeySymbols:symbols_=!symbols_;return result(C::Changed);
 case ib::KeySpace:if(value_.size()<180)value_+=' ';return result(C::Changed);
 case ib::KeyBackspace:if(!value_.isEmpty()){int n=1;if(value_.size()>=2&&value_.back().isLowSurrogate()&&value_[value_.size()-2].isHighSurrogate())n=2;value_.chop(n);}return result(C::Changed);
 case ib::KeyClear:value_.clear();return result(C::Changed);
 case ib::KeyPaste:value_+=sharedClipboard().left(std::max(0,180-int(value_.size())));return result(C::Changed);
 case ib::KeyCancel:return result(C::Cancel,true);
 case ib::KeyDone:if(value_.size()>180||(page_==Page::Rename&&value_.trimmed().isEmpty()))return {};return result(page_==Page::Rename?C::Rename:C::Folder,true);
 default:return {};}
}
void LibraryManage::paint(spike::Surface s){if(!active())return;ib::Canvas c(s);targets_.clear();
 if(page_==Page::Menu){std::vector<ib::sheets::MenuItem> items;
  if(document_.trashed){items.push_back({Restore,ib::Icon::Restore,"Restore to My files"});items.push_back({Delete,ib::Icon::Trash,"Delete permanently",true});}
  else{items.push_back({Favorite,favorite_?ib::Icon::StarFilled:ib::Icon::Star,favorite_?"Remove from Favorites":"Add to Favorites"});items.push_back({Rename,ib::Icon::Note,"Rename"});items.push_back({Move,ib::Icon::Folder,"Move to folder"});items.push_back({Trash,ib::Icon::Trash,"Move to Trash",true});}
  spike::Rect anchor{ib::library::ContentLeft,520,ib::library::ContentRight,560};
  // Title card above the menu so it is clear which book is being managed.
  spike::Rect head{anchor.x1-640,anchor.y0-150,anchor.x1,anchor.y0-10};ib::panel(c,head,t::RadiusMedium);
  ib::text::draw(c,{ib::Face::Serif,ib::theme::Body.size},float(head.x0+t::S4),float(head.y0+62),ib::text::fit({ib::Face::Serif,ib::theme::Body.size},utf8(document_.title),float(ib::width(head)-t::S5-t::MinTarget)),t::InkPrimary);
  ib::text::draw(c,{ib::Face::Text,float(t::Caption.size)},float(head.x0+t::S4),float(head.y0+104),document_.folder.isEmpty()?"My files":utf8("My files / "+document_.folder),t::InkSecondary);
  ib::iconButton(c,targets_,Close,{head.x1-t::MinTarget,head.y0+20,head.x1,head.y1-20},ib::Icon::Close);
  ib::sheets::menu(c,targets_,{anchor.x1-640,anchor.y0-10,anchor.x1,anchor.y0},items,640);return;}
 if(page_==Page::ConfirmDelete){ib::sheets::notice(c,targets_,{"Delete permanently?","Inkbound's copy of “"+utf8(document_.title)+"” and all its notes, highlights and handwriting will be deleted. Your original file is not affected. This cannot be undone.",{{CancelDelete,ib::Icon::Close,"Cancel"},{ConfirmDelete,ib::Icon::Trash,"Delete",true}}});return;}
 ib::KeyboardModel k;k.title=page_==Page::Rename?"Rename book":"Folder (empty for My files)";k.value=utf8(value_);k.hint=page_==Page::Rename?"Title":"Folder name";k.shift=upper_;k.symbols=symbols_;k.paste=!sharedClipboard().isEmpty();ib::keyboard(c,targets_,k);
}
}
