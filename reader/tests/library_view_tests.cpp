#include "library_view.h"
#include "bookmarks.h"
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using ux::LibraryView;
namespace L=ib::library;
static void check(bool v,const char* name){if(!v)throw std::runtime_error(name);std::cout<<name<<"=PASS\n";}
static QString id(int n){return QString::number(n,16).rightJustified(64,'0');}
// Views record their touch targets while painting (as on device): paint, then tap a target.
static std::vector<std::uint32_t> screen(1632*2160);
static LibraryView::Hit tap(LibraryView& v,int target,const QVector<library::Document>& docs){v.paint({screen,1620,2160,1632},docs,"/nonexistent");auto r=v.targets().rect(target);
 /* Sections live in the navigation drawer: open it with the menu button first, as a finger would. */
 if(r.empty()&&target>=ib::library::NavBase&&target<ib::library::NavBase+int(ib::library::Section::Count)){auto m=v.targets().rect(ib::library::Menu);if(m.empty())return {};v.hit((m.x0+m.x1)/2,(m.y0+m.y1)/2);v.paint({screen,1620,2160,1632},docs,"/nonexistent");r=v.targets().rect(target);}
 if(r.empty())return {};return v.hit((r.x0+r.x1)/2,(r.y0+r.y1)/2);}
int main(int argc,char**argv){QCoreApplication application(argc,argv);LibraryView view;QVector<library::Document> none;
 check(view.visibleIds().isEmpty()&&view.offset()==0,"empty_visible");
 view.paint({screen,1620,2160,1632},none,"/nonexistent");check(view.hit(800,900).action==LibraryView::Action::None&&view.hit(-1,500).action==LibraryView::Action::None,"empty_and_outside_no_open");
 // Unfiled books sit at the root with the folders; filed books live in their folder.
 QVector<library::Document> docs;for(int i=0;i<13;++i){library::Document d;d.id=id(i+1);d.title=(i==0||i==3)?"ALPHA":"BOOK "+QString::number(i).rightJustified(2,'0');d.format="pdf";d.pages=20;d.opened=i;d.folder=i%3==0?QString():i%3==1?"Home":"Work";docs.push_back(d);}
 auto deleted=docs[0];deleted.id=id(99);deleted.trashed=true;deleted.title="TRASHED";docs.push_back(deleted);
 view.setDocuments(docs);auto ids=view.visibleIds();check(ids.size()==5&&ids[0]==id(13)&&ids[4]==id(1),"root_unfiled_recent_first");
 auto selected=tap(view,L::DocumentBase,docs);check(selected.action==LibraryView::Action::Open&&selected.id==id(13)&&selected.index==12,"grid_stable_source_mapping");
 check(tap(view,L::DocumentBase+5,docs).action==LibraryView::Action::None,"grid_beyond_items_not_selectable");
 tap(view,L::FolderBase,docs);check(view.folder()=="Home"&&view.visibleIds().size()==4&&view.visibleIds()[0]==id(11),"folder_tile_opens_folder");
 tap(view,L::Up,docs);check(view.folder().isEmpty()&&view.visibleIds().size()==5,"folder_up_returns_root");
 tap(view,L::ViewMode,docs);check(!view.grid()&&view.offset()==0,"list_toggle_resets_page");
 selected=tap(view,L::DocumentBase+4,docs);check(selected.id==id(1)&&selected.index==0,"list_fifth_exact_mapping");
 tap(view,L::Sort,docs);check(!view.recent()&&view.visibleIds()[0]==id(1)&&view.visibleIds()[1]==id(4),"title_sort_duplicate_stable_ids");
 tap(view,L::Search,docs);check(tap(view,ib::KeyBase+'a',docs).action==LibraryView::Action::Changed&&view.query()=="a"&&view.visibleIds().size()==2,"touch_keyboard_search_spans_folders");
 check(view.effectScene()==1,"keyboard_scene");tap(view,ib::KeyDone,docs);check(view.effectScene()==0&&view.query()=="a","keyboard_done_keeps_query");
 tap(view,L::ClearSearch,docs);check(view.query().isEmpty()&&view.visibleIds().size()==5,"clear_search");
 tap(view,L::NavBase+int(LibraryView::Section::Trash),docs);check(view.trash()&&view.folder().isEmpty()&&view.visibleIds().size()==1&&view.visibleIds()[0]==id(99),"trash_section");
 tap(view,L::NavBase+int(LibraryView::Section::Recents),docs);check(view.visibleIds()[0]==id(13)&&!view.visibleIds().contains(id(1)),"recents_opened_only");
 view.setFavorites({id(8)});tap(view,L::NavBase+int(LibraryView::Section::Favorites),docs);check(view.visibleIds().size()==1&&view.visibleIds()[0]==id(8),"favorites_section");
 check(tap(view,L::NavBase+int(LibraryView::Section::Tom),docs).action==LibraryView::Action::Tom,"tom_section_action");
 check(tap(view,L::NavBase+int(LibraryView::Section::Shared),docs).action==LibraryView::Action::Shared,"shared_section_action");
 tap(view,L::NavBase+int(LibraryView::Section::MyFiles),docs);tap(view,L::Search,docs);tap(view,ib::KeySymbols,docs);tap(view,ib::KeyBase+'1',docs);tap(view,ib::KeyBase+'1',docs);check(view.query()=="11"&&view.visibleIds().size()==1,"numeric_search");
 tap(view,ib::KeyBackspace,docs);check(view.query()=="1","keyboard_backspace");tap(view,ib::KeyClear,docs);check(view.query().isEmpty(),"keyboard_clear");tap(view,ib::KeyDone,docs);
 check(tap(view,L::Menu,docs).action==LibraryView::Action::Changed&&view.effectScene()==2,"menu_opens");check(tap(view,L::Menu,docs).action==LibraryView::Action::Changed&&view.effectScene()==0,"menu_closes");
 check(tap(view,L::Create,docs).action==LibraryView::Action::Changed&&view.effectScene()==2,"create_menu_opens");check(tap(view,901,docs).action==LibraryView::Action::NewNotebook,"create_new_notebook");
 check(tap(view,L::Create,docs).action==LibraryView::Action::Changed&&view.targets().rect(900).empty(),"inbox_import_is_automatic_not_a_menu_action");check(tap(view,L::Create,docs).action==LibraryView::Action::Changed,"close_create_menu");
 // Notes section shows caller-supplied notes and opens their source.
 check(tap(view,L::NavBase+int(LibraryView::Section::Notes),docs).action==LibraryView::Action::NotesChanged,"notes_section_requests_notes");
 LibraryView::NoteEntry note;note.document=id(5);note.noteId="n1";note.title="Reflection";note.excerpt="What feels natural";note.linked=true;view.setNotes({note});
 selected=tap(view,3000+ib::notes::EntryBase,docs);check(selected.action==LibraryView::Action::OpenNote&&selected.id==id(5)&&view.note(selected.note)&&view.note(selected.note)->noteId=="n1","note_opens_with_source");
 // Stars: a star tap names the note; the Favorites filter shows only starred notes.
 LibraryView::NoteEntry other=note;other.noteId="n2";other.title="Other";view.setNotes({note,other});tap(view,L::NavBase+int(LibraryView::Section::Notes),docs);
 selected=tap(view,3000+ib::notes::StarBase+1,docs);check(selected.action==LibraryView::Action::NoteFavorite&&view.note(selected.note)&&view.note(selected.note)->noteId=="n2","note_star_tap_names_note");
 other.favorite=true;view.setNotes({note,other});tap(view,3000+ib::notes::FilterBase+int(ib::notes::Filter::Favorites),docs);
 selected=tap(view,3000+ib::notes::EntryBase,docs);check(selected.action==LibraryView::Action::OpenNote&&view.note(selected.note)->noteId=="n2"&&tap(view,3000+ib::notes::EntryBase+1,docs).action==LibraryView::Action::None,"favorites_filter_only_starred");
 tap(view,3000+ib::notes::FilterBase+int(ib::notes::Filter::Tom),docs);check(tap(view,3000+ib::notes::EntryBase,docs).action==LibraryView::Action::None,"tom_filter_after_favorites_chip");
 tap(view,3000+ib::notes::FilterBase+int(ib::notes::Filter::All),docs);
 // Bookmarks: exact in their own layout, proportional (approximate) after a reflow; persisted.
 {QTemporaryDir marks;check(marks.isValid(),"bookmark_fixture_directory");auto file=std::filesystem::path(marks.path().toStdString())/"ui-bookmarks.json";ux::Bookmarks b;check(!b.toggle("book","layout-a",3,10),"unloaded_bookmarks_never_save");b.load(file);
  check(b.toggle("book","layout-a",3,10)&&b.toggle("book","layout-a",9,10)&&b.has("book","layout-a",3)&&!b.has("book","layout-b",3),"bookmark_toggle_on");
  auto same=b.places("book","layout-a",10);check(same.size()==2&&same[0].page==3&&!same[0].approximate&&same[1].page==9,"bookmark_exact_in_own_layout");
  auto reflow=b.places("book","layout-b",19);check(reflow.size()==2&&reflow[0].page==6&&reflow[0].approximate&&reflow[1].page==18&&reflow[1].approximate,"bookmark_proportional_after_reflow");
  {ux::Bookmarks again;again.load(file);check(again.has("book","layout-a",9)&&again.places("book","layout-a",10).size()==2,"bookmarks_reload");}
  check(b.toggle("book","layout-a",3,10)&&!b.has("book","layout-a",3),"bookmark_toggle_off");check(!b.toggle("book","layout-a",10,10)&&!b.toggle("","layout-a",1,10),"invalid_bookmark_refused");}
 // Paging is bounded at both ends.
 QVector<library::Document> many;for(int i=0;i<30;++i){library::Document d;d.id=id(200+i);d.title="P "+QString::number(i);d.format="epub";d.pages=10;d.opened=i;many.push_back(d);}
 LibraryView pages;pages.setDocuments(many);int first=pages.visibleIds().size();check(first>0&&first<30,"paged_grid");tap(pages,L::Next,many);check(pages.offset()==first,"grid_next");
 for(int n=0;n<10;++n)tap(pages,L::Next,many);check(pages.offset()<30&&pages.offset()+pages.visibleIds().size()==30,"last_page_bounded");
 for(int n=0;n<10;++n)tap(pages,L::Prev,many);check(pages.offset()==0,"first_page_bounded");
 view.setDocuments({});check(view.offset()==0&&view.visibleIds().isEmpty(),"catalogue_shrink_clamps_offset");
 // Cover files are read without following links and within size and dimension bounds.
 QTemporaryDir temp;check(temp.isValid(),"cover_fixture_directory");std::filesystem::path root=temp.path().toStdString();auto folder=root/"documents"/id(1).toStdString();std::filesystem::create_directories(folder);
 std::vector<std::uint32_t> pixels(1620*2160);spike::Surface surface{pixels,1620,2160,1620};QVector<library::Document> single{docs[0]};
 auto paintFresh=[&]{LibraryView painting;painting.paint(surface,single,root);};paintFresh();auto fallback=pixels;
 QImage black(10,10,QImage::Format_RGB32);black.fill(Qt::black);check(black.save(QString::fromStdString((root/"image.png").string()),"PNG"),"png_fixture");
 std::filesystem::create_symlink(root/"image.png",folder/"cover.png");paintFresh();check(pixels==fallback,"symlink_cover_fallback");std::filesystem::remove(folder/"cover.png");
 {std::ofstream out(folder/"cover.png");out<<'x';}std::filesystem::resize_file(folder/"cover.png",1024*1024+1);paintFresh();check(pixels==fallback,"oversize_cover_fallback");std::filesystem::remove(folder/"cover.png");
 std::filesystem::copy_file(root/"image.png",folder/"cover.png");paintFresh();check(pixels!=fallback,"bounded_png_cover_drawn");
 std::filesystem::remove(folder/"cover.png");QImage large(1025,1,QImage::Format_RGB32);large.fill(Qt::black);check(large.save(QString::fromStdString((folder/"cover.png").string()),"PNG"),"wide_png_fixture");paintFresh();check(pixels==fallback,"dimension_bound_before_decode");
 // A PDF's folded corner is on the cover it folds: beside a cover narrower than its tile it showed as a stray mark.
 std::filesystem::remove(folder/"cover.png");QImage tall(60,400,QImage::Format_RGB32);tall.fill(Qt::black);check(tall.save(QString::fromStdString((folder/"cover.png").string()),"PNG"),"tall_png_fixture");paintFresh();
 {int x0=1620,x1=-1,y0=2160;for(int y=0;y<2160;++y)for(int x=0;x<1620;++x)if(pixels[std::size_t(y)*1620+x]==0xff000000u){x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);}
  check(x1>x0,"tall_cover_drawn");bool clear=true;for(int y=y0;y<y0+60;++y)for(int x=x1+4;x<std::min(1620,x1+200);++x)clear&=pixels[std::size_t(y)*1620+x]==0xffffffffu;check(clear,"pdf_fold_stays_on_the_cover");}
 std::cout<<"LIBRARY_VIEW_TESTS_PASS\n";
}
