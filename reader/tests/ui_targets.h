// Test helpers: drive the Inkbound UI through the touch targets it painted,
// exactly as a finger tap on the device would.
#pragma once
#include "ux_shell.h"
namespace uitest {
inline bool tap(ux::Shell& s,spike::Rect r){if(r.empty())return false;return s.finger((r.x0+r.x1)/2,(r.y0+r.y1)/2,false);}
inline spike::Rect centre(spike::Rect r){return r;}
// Chrome (top bar, rail, footer) targets; the caller shows chrome first.
inline bool chrome(ux::Shell& s,int id){return tap(s,s.chromeTargets().rect(id));}
// The document menu opens from the toolbar's bottom tile.
inline bool menu(ux::Shell& s,int id){if(!s.menuOpen()&&!chrome(s,ib::reader::RailMore))return false;return tap(s,s.menuTargets().rect(id));}
// Closes the pen palette: the active shortcut tile toggles it; with the eraser on, a tap on the page outside it.
inline spike::Rect toolsClose(const ux::Shell& s){auto r=s.toolsView().closeRect();return r.empty()?spike::Rect{1300,1700,1340,1740}:r;}
inline bool openSettings(ux::Shell& s){return menu(s,ux::MenuSettings);}
// Writing tools: the rail pen opens the pen panel (a first tap may switch back from highlighter/eraser).
inline bool openTools(ux::Shell& s){if(s.toolsView().active())return true;if(!chrome(s,ib::reader::RailPen))return false;if(s.toolsView().active())return true;s.tick();return chrome(s,ib::reader::RailPen)&&s.toolsView().active();}
inline bool setting(ux::Shell& s,int id){return tap(s,s.viewControls().target(id));}
// Library targets; sections live in the navigation drawer, which is opened first when needed.
inline bool library(ux::Shell& s,int id){auto r=s.libraryView().targets().rect(id);
 if(r.empty()&&id>=ib::library::NavBase&&id<ib::library::NavBase+int(ib::library::Section::Count)){if(!tap(s,s.libraryView().targets().rect(ib::library::Menu)))return false;r=s.libraryView().targets().rect(id);}
 return tap(s,r);}
inline bool libraryHold(ux::Shell& s,int id){auto r=s.libraryView().targets().rect(id);return !r.empty()&&s.finger((r.x0+r.x1)/2,(r.y0+r.y1)/2,true);}
inline bool manage(ux::Shell& s,int id){return tap(s,s.libraryManage().targets().rect(id));}
// Library menu entry "Leave Inkbound" (menu ids are the library view's).
inline bool leave(ux::Shell& s){return library(s,ib::library::Menu)&&library(s,903);}
// In a D2 user-ended session the menu offers Wi-Fi (904) in place of "Leave Inkbound" (903).
inline bool wifi(ux::Shell& s){return library(s,ib::library::Menu)&&s.libraryView().targets().rect(903).empty()&&library(s,904);}
// The Wi-Fi screen's Close stays live while a scan runs.
inline bool closeWifi(ux::Shell& s){return s.finger(1425,105,false);}
inline bool tom(ux::Shell& s,int id){return s.tomPanel()&&tap(s,s.tomPanel()->targets().rect(id));}
inline bool notes(ux::Shell& s,int id){return s.notesPanel()&&tap(s,s.notesPanel()->targets().rect(id));}
inline spike::Rect tomRect(ux::Shell& s,int id){return s.tomPanel()?s.tomPanel()->targets().rect(id):spike::Rect{};}
inline spike::Rect notesRect(ux::Shell& s,int id){return s.notesPanel()?s.notesPanel()->targets().rect(id):spike::Rect{};}
// Types on the Inkbound keyboard of a panel: shift for capitals, the symbol layer for punctuation.
template<class Keys>inline bool type(ux::Shell& s,Keys keys,const QString& text,bool done=true){
 for(QChar c:text){
  if(c==' '){if(!tap(s,keys().rect(ib::KeySpace)))return false;continue;}
  bool upper=c.isUpper(),symbol=!c.isLetter();if(upper&&!tap(s,keys().rect(ib::KeyShift)))return false; // shifted keys carry their capital code
  if(symbol&&!c.isDigit()&&keys().rect(ib::KeyBase+c.toLatin1()).empty()){if(!tap(s,keys().rect(ib::KeySymbols)))return false;bool ok=tap(s,keys().rect(ib::KeyBase+c.toLatin1()));if(!tap(s,keys().rect(ib::KeySymbols))||!ok)return false;continue;}
  if(symbol&&keys().rect(ib::KeyBase+c.toLatin1()).empty()){if(!tap(s,keys().rect(ib::KeySymbols)))return false;bool ok=tap(s,keys().rect(ib::KeyBase+c.toLatin1()));if(!tap(s,keys().rect(ib::KeySymbols))||!ok)return false;continue;}
  if(!tap(s,keys().rect(ib::KeyBase+c.toLatin1())))return false;}
 return !done||tap(s,keys().rect(ib::KeyDone));}
inline bool typeTom(ux::Shell& s,const QString& text,bool done=true){return s.tomPanel()&&type(s,[&]()->const ib::Targets&{return s.tomPanel()->keyboardTargets();},text,done);}
inline bool typeNotes(ux::Shell& s,const QString& text,bool done=true){return s.notesPanel()&&type(s,[&]()->const ib::Targets&{return s.notesPanel()->keyboardTargets();},text,done);}
}
