#include "companion_http.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>
#include <QSet>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <exception>
namespace companion {namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void validatePath(const QString& p){if(p.startsWith("/v1/device/jobs?exclude=")){auto ids=p.mid(QString("/v1/device/jobs?exclude=").size()).split(',');static const QRegularExpression uuid("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\z");QSet<QString> seen;require(!ids.isEmpty()&&ids.size()<=96,"invalid delivery exclusions");for(const auto& id:ids){require(uuid.match(id).hasMatch()&&!seen.contains(id),"invalid delivery exclusions");seen.insert(id);}return;}static const QRegularExpression path("^/v1/[A-Za-z0-9_./-]+(?:\\?after=[0-9]+)?\\z");require(path.match(p).hasMatch()&&!p.contains("..")&&!p.contains("//"),"invalid companion path");}
void validateHeaders(const QMap<QByteArray,QByteArray>& h){require(h.size()<=32,"header count limit");static const QRegularExpression token("^[A-Za-z0-9!#$%&'*+.^_`|~-]+\\z");QSet<QByteArray> names;for(auto i=h.begin();i!=h.end();++i){require(i.key().size()<=128&&token.match(QString::fromLatin1(i.key())).hasMatch(),"invalid header name");auto k=i.key().toLower();require(!names.contains(k),"duplicate header name");names.insert(k);require(k!="host"&&k!="content-length"&&k!="transfer-encoding"&&k!="connection"&&k!="proxy-authorization","reserved header");require(i.value().size()<=4096,"header value limit");for(auto c:i.value())require(static_cast<unsigned char>(c)>=32&&c!=127,"invalid header value");}}
}
VerifiedHttpTransport::VerifiedHttpTransport():origin_(QStringLiteral("https://inkbound-companion.bridgecrm-zayway.workers.dev")){}
VerifiedHttpTransport::VerifiedHttpTransport(ForTests,const QUrl& o,const QSslCertificate& ca):origin_(o),fixtureCA_(ca){require(o.isValid()&&o.scheme()=="https"&&(o.host()=="localhost"||o.host()=="127.0.0.1"||o.host()=="::1")&&o.userInfo().isEmpty()&&o.path().isEmpty()&&!o.hasQuery()&&!o.hasFragment()&&!ca.isNull(),"invalid TLS fixture origin");}
HttpResponse VerifiedHttpTransport::request(const QByteArray& method,const QString& path,const QByteArray& body,const QMap<QByteArray,QByteArray>& headers,qint64 maximum,int timeout){return perform(method,path,body,headers,maximum,timeout,nullptr);}
HttpResponse VerifiedHttpTransport::stream(const QString& path,const QMap<QByteArray,QByteArray>& headers,qint64 maximum,int timeout,const HttpChunkSink& sink){require(bool(sink),"HTTP stream sink missing");return perform("GET",path,{},headers,maximum,timeout,&sink);}
HttpResponse VerifiedHttpTransport::perform(const QByteArray& method,const QString& path,const QByteArray& body,const QMap<QByteArray,QByteArray>& headers,qint64 maximum,int timeout,const HttpChunkSink* sink){
 require(method=="GET"||method=="POST","unsupported HTTP method");validatePath(path);validateHeaders(headers);require(maximum>=0&&maximum<=67108864&&body.size()<=67108864,"HTTP body limit");require(timeout>0&&timeout<=30000,"HTTP timeout limit");require(method!="GET"||body.isEmpty(),"GET body forbidden");require(QCoreApplication::instance()&&QSslSocket::supportsSsl(),"TLS runtime unavailable");QUrl url=origin_.resolved(QUrl(path));require(url.scheme()==origin_.scheme()&&url.host()==origin_.host()&&url.port()==origin_.port()&&url.userInfo().isEmpty(),"origin mismatch");
 QNetworkAccessManager manager;manager.setProxy(QNetworkProxy::NoProxy);QNetworkRequest req(url);req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,QNetworkRequest::AlwaysNetwork);req.setAttribute(QNetworkRequest::CacheSaveControlAttribute,false);req.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);req.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);auto ssl=QSslConfiguration::defaultConfiguration();ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);if(!fixtureCA_.isNull())ssl.setCaCertificates({fixtureCA_});req.setSslConfiguration(ssl);for(auto i=headers.begin();i!=headers.end();++i)req.setRawHeader(i.key(),i.value());
 std::unique_ptr<QNetworkReply> reply(method=="GET"?manager.get(req):manager.post(req,body));reply->setReadBufferSize(65536);QEventLoop loop;QTimer timer;timer.setSingleShot(true);HttpResponse result;const char* failure=nullptr;std::exception_ptr callbackError;qint64 received=0;
 auto fail=[&](const char* reason){if(!failure)failure=reason;reply->abort();loop.quit();};
 auto metadata=[&]{result.status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();require(result.status<300||result.status>=400,"HTTP redirect forbidden");auto pairs=reply->rawHeaderPairs();require(pairs.size()<=128,"response header count limit");int total=0;QMap<QByteArray,QByteArray> normalized;for(const auto& pair:pairs){total+=pair.first.size()+pair.second.size();require(total<=32768,"response header limit");auto key=pair.first.toLower();require(!normalized.contains(key)||normalized[key]==pair.second,"conflicting response header");normalized.insert(key,pair.second);}result.headers=normalized;if(sink&&result.status==200)require(result.headers.value("content-type").split(';').first().trimmed().toLower()=="text/event-stream","HTTP SSE content type");};
 auto read=[&]{if(failure||result.stopped)return;try{metadata();while(reply->bytesAvailable()>0){require(reply->bytesAvailable()<=maximum-received,"response body limit");auto chunk=reply->read(std::min<qint64>(65536,maximum-received));if(chunk.isEmpty())break;received+=chunk.size();if(!sink)timer.start(timeout);if(sink&&result.status==200){if(!(*sink)(chunk)){result.stopped=true;reply->abort();loop.quit();return;}}else result.body+=chunk;}}catch(...){callbackError=std::current_exception();fail("HTTP response or consumer rejected");}};
 QObject::connect(reply.get(),&QNetworkReply::metaDataChanged,&loop,[&]{if(!sink)timer.start(timeout);});
 QObject::connect(reply.get(),&QNetworkReply::uploadProgress,&loop,[&,last=qint64(0)](qint64 sent,qint64)mutable{if(!sink&&sent>last){last=sent;timer.start(timeout);}});
 QObject::connect(reply.get(),&QNetworkReply::readyRead,&loop,read);QObject::connect(reply.get(),&QNetworkReply::sslErrors,&loop,[&](const QList<QSslError>&){fail("TLS verification failed");});QObject::connect(reply.get(),&QNetworkReply::redirected,&loop,[&](const QUrl&){fail("HTTP redirect forbidden");});QObject::connect(reply.get(),&QNetworkReply::finished,&loop,&QEventLoop::quit);QObject::connect(&timer,&QTimer::timeout,&loop,[&]{
  // A live event stream (200, events flowing) that reaches its time budget is not a failure: what arrived was
  // delivered to the sink, the stream's cursor is durable, and the caller simply resumes it. Anything else is.
  if(sink&&!failure&&!result.stopped){read();if(!failure&&!result.stopped&&result.status==200){result.expired=true;reply->abort();loop.quit();return;}if(failure||result.stopped)return;}
  fail("HTTP request timed out");});timer.start(timeout);if(!reply->isFinished())loop.exec();timer.stop();if(!failure&&!result.stopped&&!result.expired)read();if(callbackError)std::rethrow_exception(callbackError);require(!failure,failure?failure:"HTTP request failed");metadata();require(result.status>=100&&result.status<=599,"HTTP network failure");auto error=reply->error();require(result.stopped||result.expired||error==QNetworkReply::NoError||(result.status>=400&&error>=QNetworkReply::ContentAccessDenied),"HTTP network failure");return result;
}
}
