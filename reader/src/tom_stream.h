#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <functional>
namespace companion {
struct TomEvent {quint64 sequence{};QString kind;QJsonObject data;};
// Return true only after durably applying BOTH event and sequence, idempotently.
using TomEventSink=std::function<bool(const TomEvent&)>;
class TomStream {
 quint64 cursor_{};QByteArray line_,event_,id_,data_,lastFingerprint_;bool afterCR_{},stopped_{},terminal_{};qsizetype eventBytes_{},textChars_{};unsigned accepted_{};
 bool line(const TomEventSink&);bool dispatch(const TomEventSink&);
public:
 explicit TomStream(quint64 lastDurableSequence=0):cursor_(lastDurableSequence){}
 bool feed(const QByteArray&,const TomEventSink&);
 // Drops only unfinished wire bytes; accepted cursor and partial answer stay owned by caller.
 void reconnect();quint64 cursor()const{return cursor_;}bool terminal()const{return terminal_;}bool stopped()const{return stopped_;}
};
}
