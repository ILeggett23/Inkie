#pragma once
#include "companion_client.h"
#include "tom_stream.h"
#include <QJsonArray>
#include <QStringList>
#include <filesystem>
#include <memory>
namespace companion {
struct TomDraft {
 QString question,correctedText;QJsonArray originalHandwriting;
 QString scope{"general"};QJsonObject context;QString localDocumentId,wireAlias;
 QString handwritingImage,pageImage;
 QString interpretation,interpretationTurn;
};
class TomHistory {
 friend class TomStore;TomHistory()=default;QJsonArray messages_;QString conversation_,device_;QByteArray provenance_;QStringList turns_;int excluded_{};
public:
 const QJsonArray& messages()const{return messages_;}const QStringList& includedTurns()const{return turns_;}int excludedTurns()const{return excluded_;}
};
class TomCreateAttempt {
 friend class TomStore;TomCreateAttempt()=default;std::shared_ptr<const FrozenTomRequest> request_;QString device_;
public:
 const FrozenTomRequest& request()const{return *request_;}const QString& deviceId()const{return device_;}
};
class TomStore {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 static constexpr qint64 MaxTotalBytes=64*1024*1024;
 explicit TomStore(std::filesystem::path libraryRoot);~TomStore();TomStore(const TomStore&)=delete;
 // How opening went, for the helper's TOM_HELPER_TIMING journal: the store's bytes and turns, how many settled turns
 // were checked (this process had not validated them yet) and how long opening took.
 struct OpenStats {qint64 bytes{};unsigned turns{},checked{};long long ms{};};OpenStats opened()const;
 QString createConversation(QString title);
 QString saveDraft(const QString& conversation,const TomDraft&,QString turn={});
 void linkInterpretation(const QString& conversation,const QString& interpretationTurn,const QString& sourceDraftTurn);
 bool applyInterpretation(const QString& conversation,const QString& interpretationTurn);
 // purpose: of the request the history is for (read-answer binds its draft without the handwriting image).
 TomHistory history(const QString& conversation,const QString& device,const TomDraft& target,const QString& purpose={})const;
 FrozenTomRequest reviewedRequest(const QString& conversation,const QString& turn,const TomConfig&,const QString& device,const QJsonObject& wire,bool consent,bool imagesDisclosed,const TomHistory&)const;
 void freezeForCreate(const QString& conversation,const QString& turn,const FrozenTomRequest&,const TomConfig&,const QString& device,const TomHistory* history=nullptr);
 // MUST succeed before any create POST. A second attempt is forbidden, including after a lost response.
 TomCreateAttempt markCreateAttempt(const QString& conversation,const QString& turn);
 void confirmCreate(const QString& conversation,const QString& turn,const QJsonObject& acknowledgement);
 // Atomic answer + sequence + replay fingerprint replacement. Safe as a durable SSE callback.
 bool applyEvent(const QString& conversation,const QString& turn,const QString& expectedRequestId,const TomEvent&);
 void recordStatus(const QString& conversation,const QString& turn,const QJsonObject& remoteStatus);
 void recordFailure(const QString& conversation,const QString& turn,const QString& safeCode);
 void markCancelAttempt(const QString& conversation,const QString& turn);
 QString explicitRetry(const QString& conversation,const QString& originalTurn,const FrozenTomRequest&,const TomConfig&,const QString& device,bool renewedBillableConsent,const TomHistory* history=nullptr);
 QJsonObject deleteConversation(const QString& conversation,bool confirmed);
 // Read-only views take no writer lock and never load imagery or frozen wire bodies.
 static QJsonObject listConversations(const std::filesystem::path& libraryRoot);
 static QJsonObject readSnapshot(const std::filesystem::path& libraryRoot,const QString& conversation,unsigned recentTurns=20);
 static QJsonObject readTurn(const std::filesystem::path& libraryRoot,const QString& conversation,const QString& turn);
 TomDraft draft(const QString& conversation,const QString& turn)const;
 enum class Fault {None,BeforeReplace,AfterReplace};void injectFault(Fault);
};
// Inkie's long-term memory: short notes about the reader ("The reader's name is Sam."), kept on this tablet in
// <library>/inkie/memory.json, oldest first. The service proposes them from what the reader says (a `memory` event of
// "+ note" / "- note" lines, applied once per request); they go with each question when the service accepts them, and
// Settings → Inkie shows, forgets and clears them. At most MaxFacts notes of MaxChars, MaxTotal characters in all.
namespace memory {
inline constexpr int MaxFacts=60,MaxChars=240,MaxTotal=4096;
std::filesystem::path path(const std::filesystem::path& library);
QStringList read(const std::filesystem::path& library)noexcept; // empty when there are none or the file can't be read
bool apply(const std::filesystem::path& library,const QString& request,const QString& operations)noexcept;
bool forget(const std::filesystem::path& library,const QString& fact)noexcept;
bool clear(const std::filesystem::path& library)noexcept;
}
}
