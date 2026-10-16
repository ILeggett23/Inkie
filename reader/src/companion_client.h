#pragma once
#include <stdexcept>

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
namespace companion {
struct TomRequestUnavailable final:std::runtime_error {TomRequestUnavailable():std::runtime_error("Tom request unavailable (HTTP 404)") {}};

inline constexpr qint64 MaxFileBytes=67108864;
struct Delivery {QString jobId,documentId,filename,format,sha256,downloadToken; qint64 size{}; double expires{}; bool converted{},intentionalNew{};};
QVector<Delivery> parseManifest(const QByteArray&,double now);
// All validation throws std::runtime_error with content-free diagnostic text.
void validateRangeReply(const Delivery&,qint64 start,qint64 end,int status,const QByteArray& contentRange,const QByteArray& etag,qint64 bodyBytes);
// readAnswer: the service reads handwriting and answers it in one request (purpose read-answer). It comes from the
// X-Inkbound-Tom-Purposes response header, not the strictly checked body, and is not part of the fingerprint.
struct TomConfig {QString provider,label;QStringList models;int maxOutputTokens{},dailyLimit{};bool configured{};QByteArray fingerprint;bool readAnswer{},inkPlan{},autoAnswer{},readAutoAnswer{};QString illustrationModel;bool sceneAnswer{},readSceneAnswer{};int sceneMaxTokens{},sceneVersion{3};};
TomConfig parseTomConfig(const QByteArray&);
class TomStore;
class FrozenTomRequest {
 friend class TomStore;
 FrozenTomRequest(const TomConfig&,const QJsonObject&,bool,bool,bool provenanceVerified);
 QByteArray body_,configFingerprint_;QString automaticModel_,automaticPurpose_;int automaticLimit_{},automaticSceneVersion_{};
public:
 FrozenTomRequest(const TomConfig& reviewed,const QJsonObject& draft,bool explicitConsent,bool imagesDisclosed);
 const QByteArray& body()const{return body_;}
 QByteArray fingerprint()const;
 bool acceptsConfig(const TomConfig& current)const;
 // Direct construction requires empty general history; only TomStore reviewedRequest can authorize provenance-checked history.
 // Replay must reuse body() exactly. Deliberate billable retry requires fresh request_id and renewed consent.
};
}
