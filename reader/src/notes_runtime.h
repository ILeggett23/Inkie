// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "notes_canvas.h"
#include "illustration_request.h"
#include "stroke_playback.h"
#include <optional>
#include <QRectF>
namespace notes {
struct View {std::uint64_t revision{},canvasRevision{},contentRevision{},exports{};bool ready{},busy{},editable{},saved{};QString error,status,document,selected;QJsonObject page,doc,note,highlight;std::shared_ptr<const tomink::PlaybackPlan> playback;bool highlightCreated{},generating{},generationError{};QString generation;QJsonArray notes,selectedInk;QRectF selectedBounds;Window window;unsigned strokeCount{},pointCount{};std::shared_ptr<const std::vector<std::uint32_t>> pixels;};
class Runtime final {
 std::filesystem::path features_;Canvas& canvas_;std::shared_ptr<tomink::IllustrationProvider> illustration_;std::shared_ptr<const tomink::PlaybackPlan> playback_;bool retiredClean_{};std::thread worker_;std::atomic<bool> generationStop_{false};std::atomic<bool> stop_{false},done_{false},failed_{false},queuedCommand_{false},durabilityFailure_{false};
 struct Carried {unsigned slot{};QJsonObject stroke;bool erase{};};std::optional<Carried> carry_; /* worker-only: an eraser stroke taken while batching pen strokes */std::atomic<unsigned> commandState_{0};
 recovery::v2::progress::WorkToken commandTracking_;
 mutable std::mutex mutex_;std::optional<QJsonObject> command_,failedCommand_;View view_;QJsonObject page_;QString selected_,highlight_;bool highlightCreated_{};Window window_{};
 QJsonObject perform(product::Services&,const QJsonObject&);void work()noexcept;void update(const std::function<void(View&)>&);void publish(bool changed,bool rerender=true);
public:
 // Exact product::featureDirectory path; no catalogue or global store ownership.
 Runtime(std::filesystem::path featureDirectory,Canvas&,std::shared_ptr<tomink::IllustrationProvider> illustration={});~Runtime();bool request(QJsonObject);void cancelGeneration()noexcept{generationStop_=true;}View view()const;unsigned unsaved()const noexcept{return canvas_.unsaved()+(commandState_.load(std::memory_order_acquire)!=0);}bool failed()const noexcept{return failed_||canvas_.failed();}bool finish(unsigned ms=100)noexcept;bool retire()noexcept;bool drained()const noexcept{return !worker_.joinable();}
};
QJsonObject selectedAnchor(const QJsonObject& page,const QJsonObject& selection);
}
