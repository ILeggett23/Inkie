#pragma once
#include <QByteArray>
#include <QMap>
#include <QSslCertificate>
#include <QUrl>
#include <functional>
#include <stdexcept>
namespace companion {
struct HttpResponse {int status{};QMap<QByteArray,QByteArray> headers;QByteArray body;bool stopped{},expired{}; /* expired: a 200 event stream reached its time budget (resumable, not an error) */};
using HttpChunkSink=std::function<bool(const QByteArray&)>;
// timeoutMs is an inactivity deadline for request(); stream() has a total resumable budget.
class HttpTransport {public:virtual ~HttpTransport()=default;virtual HttpResponse request(const QByteArray& method,const QString& relativePath,const QByteArray& body,const QMap<QByteArray,QByteArray>& headers,qint64 maxResponseBytes,int timeoutMs)=0;
 virtual HttpResponse stream(const QString&,const QMap<QByteArray,QByteArray>&,qint64,int,const HttpChunkSink&){throw std::runtime_error("HTTP streaming unsupported");}
};
class VerifiedHttpTransport final:public HttpTransport {
 QUrl origin_;QSslCertificate fixtureCA_;
 HttpResponse perform(const QByteArray&,const QString&,const QByteArray&,const QMap<QByteArray,QByteArray>&,qint64,int,const HttpChunkSink*);
public:
 VerifiedHttpTransport();
 struct ForTests{};
 // Explicit loopback-only HTTPS test seam; never configure this from CLI/environment.
 VerifiedHttpTransport(ForTests,const QUrl& loopbackOrigin,const QSslCertificate& fixtureCA);
 HttpResponse request(const QByteArray&,const QString&,const QByteArray&,const QMap<QByteArray,QByteArray>&,qint64,int)override;
 HttpResponse stream(const QString&,const QMap<QByteArray,QByteArray>&,qint64,int,const HttpChunkSink&)override;
};
}
