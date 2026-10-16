#pragma once
#include "companion_client.h"
#include "tom_stream.h"
#include <memory>
namespace companion {
class Receiver;
// One explicitly reviewed request. No provider POST is retried automatically.
// This foundation keeps session state in memory; durable conversation ownership is a caller concern.
class TomSession {
 Receiver& receiver_;TomConfig config_;QString reviewedDevice_,requestId_,requestDevice_;QJsonObject draft_;
 std::unique_ptr<const FrozenTomRequest> reviewed_;std::unique_ptr<TomStream> stream_;bool attempted_{};
public:
 explicit TomSession(Receiver& r):receiver_(r){}
 TomConfig fetchConfig();void setDraft(QJsonObject);void review(bool explicitConsent,bool imagesDisclosed);
 QByteArray reviewedFingerprint()const;
 QJsonObject create();void resume(QString requestId,QString deviceId,quint64 lastDurableSequence);
 QJsonObject requestStatus();QJsonObject cancel();bool streamOnce(const TomEventSink&,int timeoutMs=30000); /* false: not finished yet (time budget reached or stopped); resume later */
 QString requestId()const{return requestId_;}quint64 cursor()const{return stream_?stream_->cursor():0;}
};
}
