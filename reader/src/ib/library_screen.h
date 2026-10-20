// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Home / My files in the native Paper Pro layout: centered wordmark under a collapsed menu (the
// navigation drawer), heading with sort and view, inline folders, a three-column thumbnail grid, and
// the bottom search/create pill.
#include "ib/components.h"
#include "ib/status_screen.h"
#include <span>
#include <string>
#include <vector>
namespace ib::library {
enum class Section:unsigned {MyFiles,Notes,Favorites,Recents,Shared,Tom,Trash,Settings,Count};
const char* sectionName(Section)noexcept;
struct Image {std::span<const std::uint32_t> pixels;int width{},height{},stride{};bool empty()const noexcept{return pixels.empty()||width<1||height<1;}};
struct Folder {std::string name;int items{};};
struct Document {std::string title,meta,format;Image cover;bool favorite{};};
// Drawer and create-menu rows supplied by the caller (ids are the caller's).
struct Item {int id{};Icon icon{};std::string label,detail;};
struct Model {
 Section section{Section::MyFiles};bool drawer{},create{};int battery{-1};bool charging{},wifi{}; /* battery: real percent, or -1 when unknown (nothing shown); wifi: link up */std::vector<Item> drawerExtras,createItems;std::string heading{"My files"},sortLabel{"Last opened"},crumb;
 bool grid{true},searching{};std::string query,notice;
 std::vector<Folder> folders;std::vector<Document> documents; // only the visible page
 int page{},pages{1};bool sharedAvailable{true},tomAvailable{true};std::string clock,clockZone;
};
// Target ids. Folder/Document ids are base + index into the model vectors.
enum Id:int {Wordmark=1,Search,Create,ViewMode,Menu,Sort,Prev,Next,ClearSearch,Up,NavBase=100,FolderBase=200,DocumentBase=400};
// Documents per page for a layout, given whether the folders strip is shown.
int capacity(bool grid,int folderCount)noexcept;
int folderCapacity()noexcept;
inline constexpr int ContentLeft=96,ContentRight=1524;
inline constexpr Rect NoticeRect{ContentLeft,292,ContentRight,334}; // one quiet line under the heading
inline constexpr Rect StatusRect=ib::status::LibraryBar; /* device status in the mast (refreshed alone when it changes) */
inline constexpr Rect DrawerRect{0,0,474,2160},PillRect{698,1992,922,2092},CreateRect{540,0,1080,1990}; // CreateRect: bottom edge; height follows its rows
Rect createMenuRect(std::size_t rows)noexcept;
// The library's layout for its frame: the portrait panel (1620x2160, the constants above) or, with the tablet turned,
// an upright 2160x1620 frame (docs/LANDSCAPE.md): wider content with four cover columns, the pill and pager at the foot,
// the drawer the frame's height, device status at its top right. paint() lays out for the canvas it is given.
struct Geometry {int width{1620},height{2160},contentLeft{ContentLeft},contentRight{ContentRight},bottomLimit{1985},gridCols{3},pagerLeft{1160};Rect notice{NoticeRect},status{StatusRect},drawer{DrawerRect},pill{PillRect};};
Geometry geometry(int width,int height)noexcept;
int capacity(bool grid,int folderCount,const Geometry&)noexcept;
void paint(Canvas&,Targets&,const Model&); // base + overlays
// Split for callers that draw section content (Notes, Settings) between the base and the overlays.
void base(Canvas&,Targets&,const Model&);void overlays(Canvas&,Targets&,const Model&); // pill, create menu, drawer
// Typographic cover used when a document has no cover image.
void placeholderCover(Canvas&,Rect,std::string_view title,std::string_view format);
}
