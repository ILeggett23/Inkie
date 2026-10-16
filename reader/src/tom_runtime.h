// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "tom_canvas.h"
#include <QJsonObject>
#include <QString>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
namespace tom {
enum class Action{Bind,Question,Correction,Clear,Interpret,Send,Stop,Retry,NextTurn,NewThread,OpenThread,DeleteThread,Consent,RefreshSources};
struct Command{Action action{};QString text;QJsonObject context;bool flag{};Command(Action value=Action::Bind,QString message={},QJsonObject frozen={},bool accepted=false):action(value),text(std::move(message)),context(std::move(frozen)),flag(accepted){}};
struct View {
 std::uint64_t revision{},canvasRevision{};bool ready{},busy{},editable{},nextWriting{},configured{},consent{},saved{},sourceMissing{},capacity{},active{},helperBusy{},unsaved{},failed{};
 bool autoReply{},automaticConfigured{};unsigned sceneMaxTokens{},sceneVersion{3};QString purpose,answerImageModel,answerReviewModel,answerQuestion,answerContext;
 bool transcription{}; // displayed exchange is a handwriting transcription, not an answer
 bool answerComplete{}; // the displayed answer is the whole reply (turn completed), not text still streaming in
 QString providerId,providerLabel,model,configFingerprint,illustrationModel;unsigned maxOutputTokens{};
 QString thread,turn,question,correctedText,answer,status{"OPENING TOM"},error,origin,interpretation,request;
 QJsonObject context;QJsonArray handwriting,threads,turns;
};
// Worker only: JSON, PNG, durable storage and helper I/O. Contact uses fixed Canvas slots.
// Acquire TomStore briefly per transaction; never retain its writer lock across network waits.
class Runtime final {
 class Worker;std::unique_ptr<Worker> worker_;std::filesystem::path application_,data_;Canvas& canvas_;const bool networkAllowed_;
 recovery::v2::progress::WorkToken bootstrapTracking_,commandTracking_;
 mutable std::mutex mutex_;View view_;Command command_;bool pendingCommand_{},completingCommand_{};std::atomic<bool> stop_{false};
public:
 Runtime(std::filesystem::path applicationPath,std::filesystem::path libraryRoot,Canvas&,bool networkAllowed=true);~Runtime();
 bool start();bool request(Command);View view()const;unsigned unsaved()const noexcept;bool failed()const noexcept;bool finish(unsigned ms=100)noexcept;bool drained()const noexcept;
};
}
