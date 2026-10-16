#pragma once
#include "epub_typography.h"
#include <utility>
#include <vector>
#include "core.h"
#include "pdf_viewport.h"
#include "ib/sheets.h"
#include <string>
namespace ux {
// Pure staged settings/command view. Never parses/renders a book or performs I/O.
class DocumentControls final {
public:
 enum class Format {Pdf,Epub};enum class Tab {View,Reading,Pages,About};
 struct Binding {std::string document,page,layout;std::uint64_t epoch{};bool operator==(const Binding&)const=default;};
 struct EpubSettings {int fontSize{12},lineWidth{600};epub::Typography typography{};bool operator==(const EpubSettings&)const=default;};
 struct Snapshot {Binding binding;Format format{Format::Pdf};int page{},pages{1};reader::pdfview::Rect sourceBounds;reader::pdfview::View pdf;EpubSettings epub;bool legacyInkPins{},busy{},contact{},quietEffects{};std::string title;std::vector<std::pair<int,bool>> bookmarks;bool layoutInk{}; /* EPUB: handwriting exists in some layout (shown only there) */};
 enum class Action {None,PreviewChanged,ApplyPdf,ApplyEpub,JumpPage,Cancel,Quiet};
 enum class Refusal {None,Stale,Busy,Pinned,Unavailable,Invalid};
 struct Command {Action action{Action::None};Binding binding;reader::pdfview::View pdf;EpubSettings epub;int targetPage{};Refusal refusal{Refusal::None};bool consumed{},quietEffects{};int bookmarkIndex{-1};};
 // Settings sheet (concept #9). Controls are touch targets recorded at paint;
 // target(id) exposes them (ib::sheets::SettingsId) for routing and tests.
 inline static constexpr spike::Rect Panel=ib::sheets::SettingsRect;
 spike::Rect target(int id)const noexcept{return targets_.rect(id);}
 void showTab(Tab t)noexcept{if(active_){tab_=t;notice_=Refusal::None;damage_=Panel;}}
 static bool valid(const Snapshot&)noexcept;
 bool open(const Snapshot&);bool update(const Snapshot&);
 void rejectApply()noexcept{pending_=false;notice_=Refusal::Invalid;damage_=Panel;}
 Command hit(int,int,const Snapshot& current);
 bool paint(spike::Surface)noexcept;
 bool active()const noexcept{return active_;}bool pending()const noexcept{return pending_;}Tab tab()const noexcept{return tab_;}
 const Snapshot& applied()const noexcept{return applied_;}
 const reader::pdfview::View& previewPdf()const noexcept{return pdf_;}
 EpubSettings previewEpub()const noexcept{return epub_;}
 int selectedPage()const noexcept{return selectedPage_;}int firstVisiblePage()const noexcept{return firstPage_;}
 int pageForCell(unsigned slot)const noexcept{return slot<unsigned(ib::sheets::OverviewPageSize)&&firstPage_+int(slot)<applied_.pages?firstPage_+int(slot):-1;}
 spike::Rect takeDamage()noexcept{auto r=damage_;damage_={};return r;}
 // Page overview images (rendered by the thumbnail helper, owned by the shell); read only while painting.
 void setPageImages(std::function<ib::sheets::PageImage(int)> source){pageImage_=std::move(source);}
private:
 ib::Targets targets_;std::function<ib::sheets::PageImage(int)> pageImage_;Snapshot applied_;reader::pdfview::View pdf_;EpubSettings epub_;int selectedPage_{},firstPage_{},selectedBookmark_{-1};Tab tab_{Tab::View};bool active_{},pending_{};Refusal notice_{};spike::Rect damage_{};
 void resetPreview();bool stagePdf(reader::pdfview::View);bool changed()const noexcept;
};
}
