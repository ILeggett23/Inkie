#include "tom_session.h"
#include "receiver.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <stdexcept>
namespace companion {namespace {void need(bool b,const char* m){if(!b)throw std::runtime_error(m);}bool uuid(const QString& s){return QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\z").match(s).hasMatch();}}
TomConfig TomSession::fetchConfig(){need(!attempted_,"Tom request identity already allocated; use status or resume");reviewed_.reset();config_=TomConfig{};reviewedDevice_=receiver_.status()["device_id"].toString();config_=receiver_.fetchTomConfig(reviewedDevice_);return config_;}
void TomSession::setDraft(QJsonObject draft){need(!attempted_,"Tom sent request is immutable");draft_=std::move(draft);reviewed_.reset();}
void TomSession::review(bool consent,bool images){reviewed_.reset();need(!attempted_&&!config_.fingerprint.isEmpty()&&receiver_.status()["device_id"]==reviewedDevice_,"Tom review configuration or scope missing");reviewed_=std::make_unique<const FrozenTomRequest>(config_,draft_,consent,images);}
QByteArray TomSession::reviewedFingerprint()const{return reviewed_?reviewed_->fingerprint():QByteArray{};}
QJsonObject TomSession::create(){need(!attempted_&&bool(reviewed_),"Tom new review required; no automatic create retry");requestId_=QJsonDocument::fromJson(reviewed_->body()).object()["request_id"].toString();requestDevice_=reviewedDevice_;need(uuid(requestId_)&&uuid(requestDevice_),"Tom frozen identity");attempted_=true;stream_=std::make_unique<TomStream>();return receiver_.createTom(*reviewed_,requestDevice_);}
void TomSession::resume(QString id,QString device,quint64 cursor){need(!attempted_&&uuid(id)&&uuid(device)&&receiver_.status()["device_id"]==device,"Tom resume scope");requestId_=std::move(id);requestDevice_=std::move(device);attempted_=true;reviewed_.reset();stream_=std::make_unique<TomStream>(cursor);}
QJsonObject TomSession::requestStatus(){need(attempted_,"Tom request identity missing");return receiver_.getTomRequest(requestId_,requestDevice_);}
QJsonObject TomSession::cancel(){need(attempted_,"Tom request identity missing");return receiver_.cancelTom(requestId_,requestDevice_);}
bool TomSession::streamOnce(const TomEventSink& sink,int timeoutMs){need(attempted_&&bool(stream_)&&bool(sink),"Tom stream identity or durable sink missing");if(stream_->terminal())return true;stream_->reconnect();auto response=receiver_.streamTom(requestId_,requestDevice_,stream_->cursor(),[&](const QByteArray& chunk){return stream_->feed(chunk,sink);},timeoutMs);return !response.stopped&&!response.expired&&!stream_->stopped();}
}
