// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Layers (concept #8), document settings (#9) and new notebook (#10) sheets,
// plus small overlay menus. Side sheets keep part of the page visible.
#include "ib/library_screen.h"
#include <functional>
#include <string>
#include <vector>
namespace ib::sheets {
// ---- Layers -----------------------------------------------------------------
struct Layer {std::string name;bool visible{true},locked{},active{},selected{};int items{};};
struct Layers {std::vector<Layer> layers; /* top-most first */ unsigned offset{};int menuFor{-1};bool confirmDelete{},busy{};std::string background{"Document"},status;bool canUndo{},canRedo{};Rect rect{720,120,1596,1640}; /* LayersRect, or TurnedLayersRect */};
enum LayerId:int {LayersClose=1,LayerAdd,LayersPrev,LayersNext,LayersUndo,LayersRedo,MenuRename=10,MenuLock,MenuUp,MenuDown,MenuDelete,MenuActivate,
 RowBase=100,EyeBase=200,RowMenuBase=300};
inline constexpr unsigned LayerPageSize=7;
inline constexpr Rect LayersRect{720,120,1596,1640};
// Turned (docs/LANDSCAPE.md, PR 3): the same side sheet, starting at layout::TurnedSheetTop.
inline constexpr Rect TurnedLayersRect{LayersRect.x0,layout::TurnedSheetTop,LayersRect.x1,layout::TurnedSheetTop+LayersRect.y1-LayersRect.y0};
inline Rect layersRect(bool turned)noexcept{return turned?TurnedLayersRect:LayersRect;}
void layers(Canvas&,Targets&,const Layers&);
// ---- Document settings ------------------------------------------------------
enum class Tab:unsigned {View,Reading,Pages,About};
// A rendered page for Page overview: 8-bit gray, borrowed for one paint.
struct PageImage {const std::uint8_t* gray{};int width{},height{};bool empty()const noexcept{return !gray||width<1||height<1;}};
inline constexpr int OverviewColumns=4,OverviewRows=3,OverviewPageSize=OverviewColumns*OverviewRows;
struct Settings {
 Tab tab{Tab::View};bool pdf{true};
 double zoom{1};unsigned rotation{};std::array<double,4> crop{}; // percent per edge: left,right,top,bottom
 bool panAvailable{};int fontSize{12},lineWidth{600};bool quietEffects{};
 unsigned family{},spacing{1},margins{1},alignment{}; // EPUB text and layout (epub::Typography order)
 int firstPage{},pages{1},selectedPage{},currentPage{};
 std::vector<std::pair<int,bool>> bookmarks; // page, approximate (made in another layout), in page order
 bool busy{},pinned{},changed{},pending{},contact{};std::string notice;
 std::vector<std::pair<std::string,std::string>> about;
 std::function<PageImage(int page)> pageImage; // Page overview thumbnails; an empty image is a page not rendered yet
};
enum SettingsId:int {SettingsClose=1,TabBase=10,Fit=20,ZoomOut,ZoomIn,RotateLeft,RotateRight,CropReset,FontLess,FontMore,LineLess,LineMore,Quiet,
 Apply=40,Cancel,PageFirst,PagePrev,PageNext,PageLast,CropBase=50 /* edge*2+increase */,PanBase=60,BookmarkBase=70,
 FamilyBase=80,SpacingBase=83,MarginBase=86,AlignBase=89,FitWidth=95,PageCellBase=100};
inline constexpr int BookmarkChips=8;
inline constexpr Rect SettingsRect{600,120,1596,2040};
void settings(Canvas&,Targets&,const Settings&);
// ---- New notebook (add note page) ------------------------------------------
enum class Template:unsigned {Blank,Lined,Grid,Dots,Count};
const char* templateName(Template)noexcept;
// Draws a page template into a rect (also used for the generated notebook preview).
void templatePattern(Canvas&,Rect,Template,int scalePercent=100);
struct NewNotebook {Template paper{Template::Lined};std::string title,folder;int pages{8};bool busy{},importAvailable{true};std::string status;};
enum NewId:int {NewClose=1,NewCreate,NewTitle,NewFolder,NewImport,NewPagesLess,NewPagesMore,TemplateBase=10};
inline constexpr Rect NewNotebookRect{200,240,1420,1520};
void newNotebook(Canvas&,Targets&,const NewNotebook&);
// ---- Small menus --------------------------------------------------------------
struct MenuItem {int id{};Icon icon{};std::string label;bool destructive{},disabled{};MenuItem(int i,Icon c,std::string l,bool d=false,bool off=false):id(i),icon(c),label(std::move(l)),destructive(d),disabled(off){}};
// Vertical menu anchored to a rect (opens below/above, right-aligned).
Rect menu(Canvas&,Targets&,Rect anchor,const std::vector<MenuItem>&,int width=520);
// Document menu (native): attached to the toolbar's document tile; a title row, then rows with an icon,
// a label and a chevron. separator: index of the first row after the divider (items.size() for none).
inline constexpr int DocumentMenuWidth=570,DocumentMenuRow=88;
Rect documentMenu(Canvas&,Targets&,Rect tile,std::string_view title,const std::vector<MenuItem>&,std::size_t separator);
// Recents: a sheet dropped from the top edge (the swipe-down interaction) with recent books.
struct RecentBook {std::string title,meta,format;bool current{};};
inline constexpr int RecentsHeight=560;
enum RecentsId:int {RecentBase=700,RecentsLibrary=710};
Rect recents(Canvas&,Targets&,std::span<const RecentBook>);
// Quick browse (native: swipe up from the bottom edge): where you are in the book, a track to jump along, the
// point you started from, and single-page steps. Pages turn underneath with the usual swipes while it is open.
struct QuickBrowse {std::string title,position;int page{},total{1},start{};};
enum QuickBrowseId:int {QuickTrack=720,QuickReturn,QuickPrevious,QuickNext};
inline constexpr Rect QuickBrowseRect{0,1746,1620,2160};
inline constexpr Rect QuickTrackRect{160,1890,1460,1986}; // the hit band; the line is drawn through its middle
Rect quickBrowse(Canvas&,Targets&,const QuickBrowse&);
// Message/confirm card centered in bounds.
struct Notice {std::string title,body;std::vector<MenuItem> actions;};
Rect notice(Canvas&,Targets&,const Notice&,Rect bounds={0,0,1620,2160});
}
