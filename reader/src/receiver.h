#pragma once
#include "companion_http.h"
#include "library.h"
#include "companion_client.h"
#include <memory>
namespace companion {
// Off-contact coordinator only; does not schedule itself or change device wake state.
enum class ReceiverUse {Library,Interactive,Visual};
class Receiver {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 Receiver(std::filesystem::path libraryRoot,HttpTransport&,library::Inspector,ReceiverUse=ReceiverUse::Library);
 ~Receiver();Receiver(const Receiver&)=delete;Receiver& operator=(const Receiver&)=delete;
 void startPairing(QString name);void pollPairing();void approvePairing(bool);
 // Sends bounded queued state first, then at most one 256KiB download range.
 // Explicit recovery removes only the current unimported failed staging copy.
 void retryTransfer();
 void syncOnce();
 // Bounded background helper burst; every request remains at most 256KiB.
 void syncBurst();QJsonObject status()const;
 // Revokes this reader on the service (best effort) and always forgets the local pairing; documents stay.
 void unpair();
 // Whitelisted Tom routes only. Device ids are public scope bindings, never credentials.
 QJsonObject createIllustration(const QJsonObject&,const QString& device);QJsonObject illustrationStatus(const QString&,const QString& device);/* One durable 256KiB piece per call; empty means download in progress. */ QByteArray illustrationContent(const QString&,const QString& device);QByteArray illustrationBurst(const QString&,const QString& device);void cancelIllustration(const QString&,const QString& device);
 int retryAfterMs()const;
 TomConfig fetchTomConfig(const QString& expectedDevice);
 QJsonObject createTom(const FrozenTomRequest&,const QString& reviewedDevice);
 QJsonObject getTomRequest(const QString& requestId,const QString& expectedDevice);
 QJsonObject cancelTom(const QString& requestId,const QString& expectedDevice);
 HttpResponse streamTom(const QString& requestId,const QString& expectedDevice,quint64 after,const HttpChunkSink&,int timeoutMs=30000);

};
}
