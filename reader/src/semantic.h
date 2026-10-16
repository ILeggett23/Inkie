#pragma once
#include "underlay.h"
#include "gesture_policy.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <memory>
namespace inkbound::dictionary {class DictionaryProvider;}
namespace semantic {
inline constexpr unsigned MaxChars=8192,MaxWords=2048,MaxLines=1024,MaxBlocks=256,MaxHighlights=64,MaxVocabulary=256,QueueCapacity=8;
struct Trace {unsigned text{},dictionary{},storage{},layout{},waits{};};
extern thread_local bool markerTrace;extern thread_local Trace live;
enum class Work {Text,Dictionary,Storage,Layout,Wait};void probe(Work)noexcept;
QString normalize(const QString&);QString lexical(const QString&);QString bounded(const QString&,int,bool* truncated=nullptr);
struct TextGeometry final {const QJsonObject data;explicit TextGeometry(QJsonObject d):data(std::move(d)){};bool available()const{return data["status"].toString()=="READY";}QString pageId()const{return data["pageId"].toString();}};
using Geometry=std::shared_ptr<const TextGeometry>;
struct SelectionSnapshot final {const QJsonObject data;explicit SelectionSnapshot(QJsonObject d):data(std::move(d)){};QString pageId()const{return data["pageId"].toString();}QString id()const{return data["id"].toString();}QByteArray utf8()const{return data["text"].toString().toUtf8();}};
using Selection=std::shared_ptr<const SelectionSnapshot>;
enum class ContextKind {Exact,Sentence,Paragraph,Page};
QJsonObject context(const SelectionSnapshot&,ContextKind);
enum class Action {Highlight,Define,Note,AskAI};enum class ActionSupport {Ready,NotImplemented,Unavailable};
ActionSupport support(Action,const SelectionSnapshot&);
Geometry extract(const std::filesystem::path&,int,const QByteArray&,const QJsonObject&);
Geometry decodeGeometry(const QJsonObject&,const QByteArray&);
Geometry loadGeometry(const std::filesystem::path&,const QByteArray&,QString document=underlay::FixtureHash)noexcept;
void saveGeometry(const std::filesystem::path&,const Geometry&);
Selection select(const Geometry&,unsigned firstWord,unsigned lastWord,std::uint64_t generation);
Selection hit(const Geometry&,double surfaceX,double surfaceY,std::uint64_t generation);
// Dictionary hit also spans adjacent words joined by a dash on the same line.
// Existing source geometry and persistent selection/annotation IDs stay intact.
Selection definitionHit(const Geometry&,double surfaceX,double surfaceY,std::uint64_t generation);
QJsonObject readChecked(const std::filesystem::path&,std::size_t);void writeChecked(const std::filesystem::path&,const QJsonObject&);
QJsonArray loadHighlights(const std::filesystem::path&,const Geometry&);
QJsonObject addHighlight(const std::filesystem::path&,const Geometry&,const Selection&);
QJsonObject lookupAndCapture(const std::filesystem::path& dictionary,const std::filesystem::path& data,const Selection&);
QJsonObject loadVocabulary(const std::filesystem::path&);
std::unique_ptr<inkbound::dictionary::DictionaryProvider> dictionaryProvider(const std::filesystem::path&);
struct PageState {Geometry geometry;QJsonArray highlights;bool loaded{},error{};QString errorText;};
struct Result {enum Kind {Load,Highlight,Define} kind{Load};std::shared_ptr<PageState> page;Selection selection;QJsonObject data;bool error{};QString message;};
class Worker {struct Impl;std::unique_ptr<Impl> impl_;
public:Worker(std::filesystem::path app,std::filesystem::path data);~Worker();bool request(Result::Kind,const std::shared_ptr<PageState>&,Selection={});bool poll(Result&);void beginQuiesce()noexcept;bool finish(unsigned ms);bool drained()const;unsigned pending()const;bool busy()const;bool failed()const;};
}
