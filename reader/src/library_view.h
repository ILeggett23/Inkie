#pragma once
#include "device_status.h"
#include "book_covers.h"
#include <functional>
#include "ib/settings_screen.h"
#include <optional>
#include "core.h"
#include "library.h"
#include "ib/notes_screen.h"
#include "ib/tom_screen.h"
#include <QHash>
#include <QSet>
#include <memory>
namespace ux {
// Home / My files (Inkbound concept #1). Pure view: navigation state, paint and
// touch targets. The caller owns persistence, opening and every side effect.
class LibraryView {
public:
 using Section=ib::library::Section;
 enum class Action {None,Changed,Open,Exit,Tom,Shared,Import,NewNotebook,Effects,OpenNote,NotesChanged,Favorite,Wifi,NoteFavorite,SleepCover,SleepIdle,ToolHandle,LeftHanded,ReadingProgress,SnapHighlights,TomStyle,TomHand,TomReveal,HighContrast,ReduceMotion,PlainText,PasscodeSet,PasscodeOff,TimeZone,TomMemoryForget /* index: newest first */,TomMemoryClear,SandMode};
 struct Hit {Action action{Action::None};QString id;int index{-1};int note{-1};};
 // Library-wide notes (concept #7) supplied by the caller for the Notes section.
 struct NoteEntry {QString document,noteId,title,excerpt,body,source;QJsonObject anchor;qint64 created{};int page{-1};unsigned color{4};bool favorite{},linked{},handwriting{},tom{},unresolved{};};
 void setDocuments(const QVector<library::Document>&);
 void setFavorites(QSet<QString> ids){favorites_=std::move(ids);rebuild();}
 const QSet<QString>& favorites()const{return favorites_;}
 void setNotes(QVector<NoteEntry> notes){notes_=std::move(notes);rebuild();}
 void setNotice(QString n){notice_=std::move(n);}QString notice()const{return notice_;}
 void setEnvironment(bool effects,bool shared,bool tom,bool userEnded){effects_=effects;shared_=shared;tom_=tom;userEnded_=userEnded;}
 // Sleep rows in Settings (Inkbound's own sleep settings; the Shell owns and saves them).
 void setSleep(bool cover,int idleMinutes){sleepCover_=cover;sleepIdle_=idleMinutes;}void setToolHandle(bool shown){toolHandle_=shown;}void setLeftHanded(bool left){leftHanded_=left;}void setReading(bool progress,bool snap){progress_=progress;snap_=snap;}
 // Settings > Security, Accessibility, Tom.
 void setPreferences(bool passcode,int companion,bool highContrast,bool reduceMotion,bool tomTyped,unsigned tomHand,bool tomReveal,int tomOnline,bool plainText=false,unsigned sandMode=0){sandMode_=sandMode;passcode_=passcode;companion_=companion;highContrast_=highContrast;reduceMotion_=reduceMotion;plainText_=plainText;tomTyped_=tomTyped;tomHand_=tomHand;tomReveal_=tomReveal;tomOnline_=tomOnline;}void setBattery(int percent,bool charging,bool wifi=false){battery_=percent;charging_=charging;wifi_=wifi;}
 // Settings > Inkie: what Inkie remembers (oldest first; the Shell reads and changes the notes).
 void setTomMemory(std::vector<std::string> notes){tomMemory_=std::move(notes);}
 // Settings pages: the real device readings (unknown readings leave their rows out).
 void setCoverSource(std::function<std::shared_ptr<const covers::Image>(const QString&,const QString&)> source){coverSource_=std::move(source);}
 void setTimeZone(std::string zone){timeZone_=std::move(zone);}
 void setClock(std::string time,std::string zone){clock_=std::move(time);clockZone_=std::move(zone);}
 void setDevice(const device::Battery& b,std::optional<bool> wifi,device::Storage storage,std::string firmware){device_=b;wifiLink_=wifi;storage_=storage;firmware_=std::move(firmware);}
 ib::settings::Page settingsPage()const noexcept{return settingsPage_;}
 void paint(spike::Surface,const QVector<library::Document>&,const std::filesystem::path&);
 Hit hit(int x,int y);
 QString query()const{return query_;}QString folder()const{return folder_;}Section section()const{return section_;}
 int effectScene()const noexcept{return keyboard_?1:(menu_||create_)?2:0;} /* 2: the drawer or the create menu */
 // Reveal anchor for the open keyboard or menu (inset content of that surface).
 spike::Rect effectContent()const noexcept;static spike::Rect keyboardContent()noexcept;spike::Rect menuContent()const noexcept;
 bool recent()const{return recent_;}bool trash()const{return section_==Section::Trash;}bool grid()const{return grid_;}
 int offset()const{return offset_;}QVector<QString> visibleIds()const;
 // Swipe paging (the Prev/Next controls without a tap): true when the visible page changed.
 bool page(int delta);void showSection(Section s);
 const ib::Targets& targets()const noexcept{return targets_;}
 const NoteEntry* note(int index)const{return index>=0&&index<notes_.size()?&notes_[index]:nullptr;}
private:
 QVector<library::Document> all_;QVector<int> filtered_;QVector<QString> folders_;QHash<QString,int> folderCounts_;QSet<QString> favorites_;
 QVector<NoteEntry> notes_;QVector<int> filteredNotes_,visibleNotes_;ib::notes::Filter noteFilter_{ib::notes::Filter::All};int noteOffset_{};
 QString query_,folder_,notice_;Section section_{Section::MyFiles};bool recent_{true},grid_{true},keyboard_{},shift_{},symbols_{},menu_{},create_{},effects_{},shared_{true},tom_{true},userEnded_{},sleepCover_{true},toolHandle_{true},leftHanded_{},charging_{},wifi_{},progress_{true},snap_{true};int sleepIdle_{15};int battery_{-1};
 int offset_{};ib::Targets targets_;
 unsigned sandMode_{};bool passcode_{},highContrast_{},reduceMotion_{},plainText_{},tomTyped_{},tomReveal_{true};int companion_{-1},tomOnline_{-1};unsigned tomHand_{1};std::vector<std::string> tomMemory_;bool memoryArmed_{};
 ib::settings::Page settingsPage_{ib::settings::Page::Reading};device::Battery device_{};std::optional<bool> wifiLink_;device::Storage storage_{};std::string firmware_,clock_,clockZone_,timeZone_;
 using Cover=covers::Image;std::function<std::shared_ptr<const Cover>(const QString&,const QString&)> coverSource_;QHash<QString,std::shared_ptr<const Cover>> covers_;
 int capacity()const;void rebuild();std::shared_ptr<const Cover> cover(const std::filesystem::path&,const QString&);
};
}
