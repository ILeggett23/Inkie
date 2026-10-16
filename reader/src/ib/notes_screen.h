// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Notes linked to content (concept #7) and the reading-note sheet (#17).
#include "ib/library_screen.h"
#include <string>
#include <vector>
namespace ib::notes {
// ---- Notes hub ------------------------------------------------------------
enum class Filter:unsigned {All,Linked,Pages,Favorites,Tom,Count};
struct Entry {std::string title,excerpt,body,source;std::uint32_t color{theme::Ink[4]};bool favorite{},linked{},handwriting{},tom{},unresolved{};library::Image preview;};
struct Hub {std::string heading{"Notes"},subtitle;bool layers{};Filter filter{Filter::All};std::vector<Entry> entries;int page{},pages{1};bool canCreate{true},favorites{true};std::string status;bool embedded{};int idOffset{};};
enum HubId:int {HubClose=1,HubCreate,HubPrev,HubNext,HubLayers,FilterBase=10,EntryBase=100,StarBase=200};
inline constexpr int HubPageSize=6;
// embedded=true paints inside the library content column instead of a sheet.
void hub(Canvas&,Targets&,const Hub&);
// ---- Note sheet -------------------------------------------------------------
// A note is a compact sheet over the page (the book stays in view above it): one slim header (Note, linked
// context, star, all notes, delete, close), the quoted passage, paper to write on, and one row of pen tools
// with Save. Strokes are stored relative to the writing area, so its width (1480) is what must not shrink.
inline constexpr int NoteSheetTop=1160,NoteExpandedTop=300,NoteHeader=136,NoteToolRow=104;
inline constexpr Rect NoteArea{70,NoteSheetTop+NoteHeader+64,1550,theme::ScreenHeight-NoteToolRow-16},NoteExpandedArea{70,NoteExpandedTop+NoteHeader+64,1550,theme::ScreenHeight-NoteToolRow-16};
Rect notePicker(bool expanded)noexcept; // the pen picker, opened from the tool row
struct Note {
 std::string heading,subtitle,excerpt,title,text,status;bool linked{},hasExcerpt{},unresolved{};
 unsigned color{4};bool textMode{},expanded{},editable{},busy{},confirmDelete{},sourceColor{},hasHighlight{},favorite{},favorites{};
 bool eraser{},retryInk{},confirmInkRetry{},selecting{},inkActions{},hasInkSelection{},playing{};unsigned playbackSpeed{};unsigned textOffset{};
 Instrument pen{Instrument::Fineliner};unsigned width{1};bool picker{};bool canUndo{true},canRedo{true};std::string context;bool empty{};
};
enum NoteId:int {NoteBack=40,NoteDone=1,NoteDelete,NoteSource,NoteTitle,NoteType,NoteInk,NoteText,NoteUp,NoteDown,NoteExpand,NoteUndo,NoteRedo,NoteSourceColor,NoteFavorite,NoteSave,NoteColorBase=20,NoteFinishInk=50,NoteInkSpeed,NoteSelectInk,NoteInkDelete,NoteInkSmaller,NoteInkLarger,NoteInkColor,NoteExportInk,NoteTom,NoteRetryInk,NoteEraser=80};
Rect noteArea(bool expanded)noexcept;
void note(Canvas&,Targets&,const Note&);
// ---- Highlight detail (tap on an existing highlight) -----------------------
struct HighlightDetail {std::string excerpt,source;unsigned color{4};bool confirmDelete{},busy{};std::string status;};
enum HighlightId:int {HlClose=1,HlAttachNote,HlSource,HlDelete,HlUndo,HlRedo,HlColorBase=20};
void highlight(Canvas&,Targets&,const HighlightDetail&);
}
