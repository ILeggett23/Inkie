#pragma once
#include "work_tracking.h"
#include "underlay.h"
#include "semantic_view_clip.h"
#include "product_services.h"
#include <condition_variable>
#include <mutex>
namespace styled {
inline constexpr unsigned ToolCount=9,ColorCount=8,MaxPoints=8192,QueueSlots=16; // D2: 16 (was 4): strokes queue while a batched save runs
enum class Tool:unsigned {Ballpoint,Fineliner,Pencil,Mechanical,Paintbrush,Calligraphy,Marker,Shader,Highlighter};
enum class Width:unsigned {Thin,Medium,Thick};
struct Style {Tool tool{Tool::Ballpoint};Width width{Width::Medium};unsigned color{};std::uint32_t rgb{};bool exact{};}; // exact: draw rgb; color stays the nearest ink
struct Matrix {std::array<double,6> value{1,0,0,1,0,0};bool valid()const noexcept;bool map(double,double,double&,double&,bool inverse=false)const noexcept;double scale()const noexcept;};
struct Coordinate {double x{},y{};};
const char* toolId(Tool)noexcept;const char* toolLabel(Tool)noexcept;const char* colorId(unsigned)noexcept;const char* widthId(Width)noexcept;
std::uint32_t colorRgb(unsigned)noexcept;std::uint32_t inkRgb(Style)noexcept;double diameter(Style)noexcept;
QJsonObject jsonStyle(Style);Style parseStyle(const QJsonObject&);
std::uint32_t compositeInk(std::uint32_t paper,std::uint32_t ink)noexcept;std::uint32_t combineInk(std::uint32_t below,std::uint32_t above)noexcept;
// Direct presentation adapter: all allocations happen in the constructor.
// No sensors are inferred: Point carries neither validated pressure nor tilt.
// Per-stroke union coverage preserves highlighter readability across overlap.
class Raster final {
 int width_,height_,stride_;std::vector<std::uint32_t> pixels_,under_;std::vector<unsigned char> coverage_;std::vector<std::uint32_t> stamps_;
 std::array<double,6> inverse_{};double stretch_{};
 std::uint32_t serial_{1};Style style_{};Matrix matrix_{};Coordinate previous_{};bool active_{},eraser_{};spike::Rect activeDamage_{},pending_{};
 bool line(Coordinate,Coordinate)noexcept;
public:
 Raster(int width,int height,int stride);
 bool begin(Style,Matrix,bool eraser=false)noexcept;
 bool point(Coordinate,bool first)noexcept;
 void end()noexcept{active_=false;}
 void cancel()noexcept;
 bool active()const noexcept{return active_;}
 void replace(std::span<const std::uint32_t>);
 void fill(const QJsonObject&,Matrix); /* Bounded native vector-region replay; no raster payload. */
 void beginHighlights();void highlight(const QJsonArray& quads,Matrix,unsigned color,std::optional<semantic::SourceClip> crop={},bool sameLayer=false); /* Worker-only union coverage, one bit per text-highlight color. */
 std::span<const std::uint32_t> pixels()const noexcept{return pixels_;}
 spike::Rect damage()const noexcept{return pending_;}void clearDamage()noexcept{pending_={};}
 void composite(spike::Surface,std::span<const std::uint32_t> background,spike::Rect)const noexcept;
};
struct Status {bool ready{},busy{},contact{},failed{},capacity{};unsigned queued{},saved{},rejected{};const char* label{};};
// One background worker owns JSON, UUIDs, product transactions and replay. The
// immutable page binding is captured before admission. A four-slot POD queue
// carries accepted contacts; it never waits for storage on the input thread.
class Ink final:public spike::OperationSink {
 enum class Kind {Stroke,Undo,Redo,Settings,Refresh,Reveal};
 enum class SlotState:unsigned {Free,Active,Queued,Done,Failed};
 struct Slot {Kind kind{Kind::Stroke};Style style{};Matrix matrix{};bool eraser{},option{},interrupted{};unsigned count{};std::array<Coordinate,MaxPoints> points{};std::atomic<SlotState> state{SlotState::Free};recovery::v2::progress::WorkToken tracking;};
 reader::lifecycle::Tracking tracking_;recovery::v2::progress::WorkToken bootstrapTracking_;
 std::filesystem::path catalogue_,baseline_;QJsonObject page_,geometry_;std::vector<std::uint32_t> legacy_;
 Raster raster_;std::unique_ptr<std::array<Slot,QueueSlots>> slots_;spike::Spsc<unsigned,QueueSlots+1> queue_;
 std::thread worker_;std::atomic<bool> stop_{false},done_{false},ready_{false},failed_{false},capacity_{false},editable_{true},transformPending_{false},legacyPresent_{false},legacyPinned_{false},mutationPending_{false},interrupted_{false},interruptedEraser_{false},interruptedRecovered_{false};
 std::atomic<unsigned> queued_{0},saved_{0},rejected_{0},unresolved_{0};int active_{-1};bool eraser_{};Matrix matrix_{};Style style_{};std::array<Style,ToolCount> styles_{};
 int width_,height_,stride_;mutable std::mutex resultMutex_;std::vector<std::uint32_t> result_;QJsonObject snapshot_;std::array<Style,ToolCount> resultStyles_{};Style resultStyle_{};std::atomic<bool> resultPending_{false};bool settingsPending_{},revealEnabled_{},resultReveal_{};std::array<char,192> error_{};
 void work()noexcept;bool enqueue(Kind,bool option=false)noexcept;void recordError(const char*)noexcept;
public:
 Ink(const std::filesystem::path& catalogue,const std::filesystem::path& baseline,QJsonObject page,std::span<const std::uint32_t> legacy,int width=1620,int height=2160,int stride=1632,QJsonObject geometry={},reader::lifecycle::Tracking tracking={});
 ~Ink();Ink(const Ink&)=delete;
 bool reserve()noexcept override;bool append(const spike::Point&)noexcept override;void cancel()noexcept override;
 bool drained()const noexcept{return !worker_.joinable();}
 bool tick();bool finish(unsigned milliseconds=100)noexcept;
 bool selectStyle(Style);bool selectTool(Tool);bool selectWidth(Width);bool selectColor(unsigned);bool setEraser(bool);bool undo();bool redo();
 bool setRevealEnabled(bool value){return enqueue(Kind::Reveal,value);}bool revealEnabled()const noexcept{return revealEnabled_;}
 bool reload(); // Close admission until current layers/annotations have been recomposed.
 bool setTransform(Matrix); // Intercontact only; caller must install matching document background.
 bool hasLegacyInk()const noexcept{return legacyPresent_.load();}
 unsigned highlighterColor()const noexcept{return styles_[unsigned(Tool::Highlighter)].color;}
 Style style()const noexcept{return style_;}bool eraser()const noexcept{return eraser_;}Status status()const noexcept;
 // D2: points the current stroke piece can still take (0 when no stroke is active). The display
 // closes a piece before it reaches this bound and continues the stroke in a new piece.
 std::size_t room()const noexcept{return active_<0?0:MaxPoints-(*slots_)[active_].count;}
 // Open only when reserve() can succeed (a Free/Done slot exists): a closed gate skips the stroke,
 // a refused reserve would end the session.
 bool admissionOpen()const noexcept{if(!(ready_&&!failed_&&editable_&&!transformPending_&&!mutationPending_&&queued_<QueueSlots))return false;for(const auto& slot:*slots_){auto state=slot.state.load(std::memory_order_acquire);if(state==SlotState::Free||state==SlotState::Done)return true;}return false;}
 unsigned unsaved()const noexcept{unsigned count=0;for(const auto& slot:*slots_){auto state=slot.state.load(std::memory_order_acquire);if(state==SlotState::Active||state==SlotState::Queued||state==SlotState::Failed)++count;}return count;}
 QString error()const;QJsonObject snapshot()const;unsigned unresolvedAnnotations()const noexcept{return unresolved_;}
 spike::Rect damage()const noexcept{return raster_.damage();}void clearDamage()noexcept{raster_.clearDamage();}
 void composite(spike::Surface out,std::span<const std::uint32_t> background,spike::Rect r)const noexcept{raster_.composite(out,background,r);}
 std::span<const std::uint32_t> pixels()const noexcept{return raster_.pixels();}
};
}
