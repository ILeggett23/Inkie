#pragma once
#include <QByteArray>
#include <QMap>
#include <QSslCertificate>
#include <QUrl>
#include <functional>
#include <memory>
class QNetworkAccessManager;class QThread;class QSslConfiguration;
#include <stdexcept>
namespace companion {
struct HttpResponse {int status{};QMap<QByteArray,QByteArray> headers;QByteArray body;bool stopped{},expired{}; /* expired: a 200 event stream reached its time budget (resumable, not an error) */};
using HttpChunkSink=std::function<bool(const QByteArray&)>;
// Timing only (the helper's TOM_HELPER_TIMING journal): a request's milestones as they happen. event is "start",
// "connecting" (a new connection), "encrypted" (its TLS handshake done), "sent", "response" (status line and headers)
// or "done"; status is the HTTP status when known, else 0. Never given bodies or headers.
using HttpTrace=std::function<void(const QByteArray& method,const QString& path,const char* event,int status)>;
// timeoutMs is an inactivity deadline for request(); stream() has a total resumable budget.
class HttpTransport {public:virtual ~HttpTransport()=default;virtual HttpResponse request(const QByteArray& method,const QString& relativePath,const QByteArray& body,const QMap<QByteArray,QByteArray>& headers,qint64 maxResponseBytes,int timeoutMs)=0;
 virtual HttpResponse stream(const QString&,const QMap<QByteArray,QByteArray>&,qint64,int,const HttpChunkSink&){throw std::runtime_error("HTTP streaming unsupported");}
};
class VerifiedHttpTransport final:public HttpTransport {
 QUrl origin_;QSslCertificate fixtureCA_;
 // One connection manager for the transport's life, in the thread that first used it, so successive requests (a helper
 // run's config, create, status and stream) reuse one TLS connection instead of a new handshake each.
 std::shared_ptr<QNetworkAccessManager> manager_;QThread* managerThread_{};HttpTrace trace_;
 QNetworkAccessManager& manager();QSslConfiguration tls()const;
 HttpResponse perform(const QByteArray&,const QString&,const QByteArray&,const QMap<QByteArray,QByteArray>&,qint64,int,const HttpChunkSink*);
public:
 VerifiedHttpTransport();
 struct ForTests{};
 // Explicit loopback-only HTTPS test seam; never configure this from CLI/environment.
 VerifiedHttpTransport(ForTests,const QUrl& loopbackOrigin,const QSslCertificate& fixtureCA);
 // Starts the DNS lookup, TCP connection and verified TLS handshake to the origin in Qt's network thread and returns at
 // once, so they overlap the caller's local work; the next request uses that connection. Certificate checks are the
 // same as a request's: a connection that fails them is dropped, and the request then fails on its own handshake.
 void preconnect();
 void trace(HttpTrace t){trace_=std::move(t);}
 HttpResponse request(const QByteArray&,const QString&,const QByteArray&,const QMap<QByteArray,QByteArray>&,qint64,int)override;
 HttpResponse stream(const QString&,const QMap<QByteArray,QByteArray>&,qint64,int,const HttpChunkSink&)override;
};
}
