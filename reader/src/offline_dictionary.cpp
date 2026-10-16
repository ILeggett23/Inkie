#include "offline_dictionary.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QtEndian>
#include <cerrno>
#include <stdexcept>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <QSet>
#include <algorithm>
#include <climits>
namespace inkbound::dictionary {
namespace {
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
constexpr std::uint64_t Header=32,Record=172,MaxFile=128*1024*1024;
QByteArray readAt(int fd,std::uint64_t at,unsigned size){need(size<=32768,"dictionary-read-budget");QByteArray result;result.resize(size);unsigned done=0;while(done<size){auto n=::pread(fd,result.data()+done,size-done,static_cast<off_t>(at+done));if(n<0&&errno==EINTR)continue;need(n>0,"dictionary-read-failed");done+=unsigned(n);}return result;}
std::uint32_t u32(QByteArray& a,int at){need(at>=0&&at<=a.size()-4,"dictionary-u32-bounds");return qFromLittleEndian<std::uint32_t>(a.data()+at);}
std::uint64_t u64(QByteArray& a,int at){need(at>=0&&at<=a.size()-8,"dictionary-u64-bounds");return qFromLittleEndian<std::uint64_t>(a.data()+at);}
QString normalized(std::string_view bytes){if(bytes.empty()||bytes.size()>512)return {};QStringDecoder decoder(QStringDecoder::Utf8);QString s=decoder(QByteArray(bytes.data(),int(bytes.size())));if(decoder.hasError())return {};s=s.normalized(QString::NormalizationForm_KC).toCaseFolded().simplified();s.replace(QChar(0x2019),'\'');while(!s.isEmpty()&&(s.front().isSpace()||s.front().isPunct()))s.remove(0,1);while(!s.isEmpty()&&(s.back().isSpace()||s.back().isPunct()))s.chop(1);s.replace('_',' ');if(s.size()>127||!QRegularExpression("^[a-z0-9][a-z0-9 '.-]*$").match(s).hasMatch())return {};return s;}
}
class WordNetDictionary final: public DictionaryProvider {
 struct Impl;std::unique_ptr<Impl> impl_;bool gcide_{};
public:
 explicit WordNetDictionary(const std::filesystem::path&,bool gcide=false);
 ~WordNetDictionary() override;
 std::string sourceIdentifier()const override;
 std::string sourceAttribution()const override;
 bool isAvailable()const override;
 bool isOffline()const override{return true;}
 std::vector<std::string> supportedLanguages()const override{return {"en"};}
 std::optional<DictionaryEntry> lookup(std::string_view,std::string_view)override;
};
struct WordNetDictionary::Impl {
 int fd{-1};std::uint32_t count{};std::uint64_t offset{},size{};QString state{"MISSING"},failure;unsigned reads{};
 explicit Impl(const std::filesystem::path& path){
  fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0){if(errno!=ENOENT){state="UNAVAILABLE";failure="Dictionary file could not be opened";}return;}
  try{struct stat info{};need(!::fstat(fd,&info)&&S_ISREG(info.st_mode)&&info.st_uid==getuid()&&!(info.st_mode&0022)&&info.st_nlink==1&&info.st_size>=static_cast<off_t>(Header)&&info.st_size<=static_cast<off_t>(MaxFile),"dictionary-owner-mode-size");auto h=readAt(fd,0,Header);need(h.left(8)==QByteArray("IBWN3V1\n",8),"dictionary-format-version");count=u32(h,8);offset=u64(h,16);size=u64(h,24);need(count>0&&count<=200000&&u32(h,12)==Record&&offset==Header+Record*count&&size==std::uint64_t(info.st_size)&&offset<size,"dictionary-index-header");state="READY";
  }catch(const std::exception& e){failure=e.what();state="UNAVAILABLE";::close(fd);fd=-1;}
 }
 ~Impl(){if(fd>=0)::close(fd);}
 QJsonObject find(const QString& input){auto key=input.toLatin1();unsigned lo=0,hi=count;for(unsigned step=0;lo<hi;++step){need(step<19,"dictionary-search-budget");auto middle=lo+(hi-lo)/2;auto record=readAt(fd,Header+Record*middle,Record);++reads;int terminator=record.indexOf('\0');need(terminator>0&&terminator<128,"dictionary-index-key");auto indexed=record.left(terminator);int comparison=key.compare(indexed);if(comparison<0){hi=middle;continue;}if(comparison>0){lo=middle+1;continue;}
   auto at=u64(record,128);auto length=u32(record,136);need(length>0&&length<=32768&&at>=offset&&at<=size&&length<=size-at,"dictionary-record-range");auto bytes=readAt(fd,at,length);++reads;need(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256)==record.mid(140,32),"dictionary-record-checksum");QJsonParseError error;auto parsed=QJsonDocument::fromJson(bytes,&error);need(error.error==QJsonParseError::NoError&&parsed.isObject(),"dictionary-record-json");auto entry=parsed.object();need(entry["word"].toString()==input&&!entry["headword"].toString().isEmpty()&&entry["headword"].toString().size()<=127&&entry["senses"].isArray()&&!entry["senses"].toArray().isEmpty()&&entry["senses"].toArray().size()<=128,"dictionary-record-identity");return entry;
  }return {};
 }
};
WordNetDictionary::WordNetDictionary(const std::filesystem::path& path,bool gcide):impl_(std::make_unique<Impl>(path)),gcide_(gcide){}
WordNetDictionary::~WordNetDictionary()=default;
std::string WordNetDictionary::sourceIdentifier()const{return gcide_?"gnu-gcide-0.54":"princeton-wordnet-3.0";}
std::string WordNetDictionary::sourceAttribution()const{return gcide_?"GNU GCIDE0.54; Webster1913 with contributions. GPL-3.0-or-later; notices/dictionary/LICENSE.gcide.":"WordNet 3.0; Copyright 2006 Princeton University. License: notices/dictionary/LICENSE.wordnet.";}
bool WordNetDictionary::isAvailable()const{return impl_->state=="READY";}
std::optional<inkbound::dictionary::DictionaryEntry> WordNetDictionary::lookup(std::string_view language,std::string_view word){
 impl_->reads=0;if(!isAvailable()||language!="en")return {};auto key=normalized(word);if(key.isEmpty())return {};auto data=impl_->find(key);
 if(data.isEmpty())return {};inkbound::dictionary::DictionaryEntry result;result.language="en";result.headword=data["headword"].toString().toStdString();result.sourceId=sourceIdentifier();result.sourceAttribution=sourceAttribution();result.datasetVersion=gcide_?"0.54":"3.0";if(gcide_)need(data.value("source")=="gnu-gcide-0.54","dictionary-source-binding");
 for(auto v:data["senses"].toArray()){auto sense=v.toObject();auto pos=sense["pos"].toString(),definition=sense["definition"].toString();need((QStringList{"noun","verb","adjective","adverb"}.contains(pos)||(gcide_&&QStringList{"preposition","conjunction","interjection","pronoun","other"}.contains(pos)))&&!definition.isEmpty()&&definition.size()<=8192,"dictionary-sense-invalid");result.senses.push_back({pos.toStdString(),definition.toStdString(),{}});}return result;
}
namespace {
class Cascade final:public DictionaryProvider {
 std::vector<std::unique_ptr<DictionaryProvider>> sources_;QSet<QString> context_;QString query_;bool allFailed_{};
 static QSet<QString> words(QString text){static const QSet<QString> stop{"the","and","for","from","that","this","with","was","were","are","have","has","had","not","you","your","their","there","what","which","would","should","can","into","than","been","being","also"};QSet<QString> result;auto matches=QRegularExpression("[a-z]{3,}").globalMatch(text.toCaseFolded());while(matches.hasNext()){auto word=matches.next().captured();if(!stop.contains(word))result.insert(word);}return result;}
 int score(const DictionarySense& sense,bool primary)const{const auto value=QString::fromStdString(sense.definition),lower=value.toCaseFolded();int result=primary?2:0;auto tokens=words(value);for(const auto& token:context_)if(token!=query_&&tokens.contains(token))result+=12;if(lower.startsWith("see ")||lower.startsWith("see also"))result-=80;if(lower.contains("[obs.")||lower.contains("[obsolete")||lower.contains("[archaic")||lower.contains("[rare"))result-=100;if(value.size()<16)result-=3;return result;}
public:
 Cascade(const std::filesystem::path& directory,std::string_view context){sources_.push_back(std::make_unique<WordNetDictionary>(directory/"wordnet.ibd"));sources_.push_back(std::make_unique<WordNetDictionary>(directory/"gcide.ibd",true));context_=words(QString::fromUtf8(context.data(),int(std::min<std::size_t>(context.size(),4096))));}
 std::string sourceIdentifier()const override{return "inkbound-offline-cascade-v1";}std::string sourceAttribution()const override{return "WordNet3.0 and GNU GCIDE0.54; individually attributed definitions";}bool isAvailable()const override{return !allFailed_&&std::any_of(sources_.begin(),sources_.end(),[](const auto& s){return s->isAvailable();});}bool isOffline()const override{return true;}std::vector<std::string> supportedLanguages()const override{return {"en"};}
 std::optional<DictionaryEntry> lookup(std::string_view language,std::string_view input)override{allFailed_=false;if(language!="en"||input.empty()||input.size()>512)return {};QStringDecoder decoder(QStringDecoder::Utf8);QString original=decoder(QByteArray(input.data(),int(input.size())));if(decoder.hasError())return {};QString folded;for(auto c:original.normalized(QString::NormalizationForm_KD))if(c.category()!=QChar::Mark_NonSpacing)folded+=c;auto bytes=folded.toUtf8();auto key=normalized({bytes.constData(),std::size_t(bytes.size())});if(key.isEmpty())return {};query_=key;std::optional<DictionaryEntry> best;int bestScore=INT_MIN;unsigned failed=0,attempted=0;auto consider=[&](const QString& word,const QString& pos){auto encoded=word.toUtf8();for(unsigned n=0;n<sources_.size();++n){auto& source=sources_[n];if(!source->isAvailable())continue;++attempted;std::optional<DictionaryEntry> entry;try{entry=source->lookup(language,{encoded.constData(),std::size_t(encoded.size())});}catch(...){++failed;continue;}if(!entry)continue;if(!pos.isEmpty()){entry->senses.erase(std::remove_if(entry->senses.begin(),entry->senses.end(),[&](const auto& sense){return sense.partOfSpeech!=pos.toStdString();}),entry->senses.end());}if(entry->senses.empty())continue;std::stable_sort(entry->senses.begin(),entry->senses.end(),[&](const auto&a,const auto&b){return score(a,n==0)>score(b,n==0);});int value=score(entry->senses.front(),n==0);if(!best||value>bestScore){bestScore=value;best=std::move(entry);}}};consider(key,{});if(best)return best;
 struct Rule {const char* pos;const char* suffix;const char* ending;};static constexpr Rule rules[]={{"noun","s",""},{"noun","ses","s"},{"noun","xes","x"},{"noun","zes","z"},{"noun","ches","ch"},{"noun","shes","sh"},{"noun","men","man"},{"noun","ies","y"},{"verb","s",""},{"verb","ies","y"},{"verb","es","e"},{"verb","es",""},{"verb","ed","e"},{"verb","ed",""},{"verb","ing","e"},{"verb","ing",""},{"adjective","er",""},{"adjective","est",""},{"adjective","er","e"},{"adjective","est","e"}};QSet<QString> seen;for(const auto& rule:rules){const auto suffix=QString::fromLatin1(rule.suffix);if(!key.endsWith(suffix)||key.size()<=suffix.size()+1)continue;auto candidate=key.left(key.size()-suffix.size())+QString::fromLatin1(rule.ending),identity=candidate+"/"+rule.pos;if(seen.contains(identity))continue;seen.insert(identity);consider(candidate,QString::fromLatin1(rule.pos));}allFailed_=failed>0&&attempted>0;return best;}
};
}
std::unique_ptr<DictionaryProvider> offlineDictionary(const std::filesystem::path& path){return std::make_unique<WordNetDictionary>(path);}
std::unique_ptr<DictionaryProvider> offlineDictionaryChain(const std::filesystem::path& directory,std::string_view context){return std::make_unique<Cascade>(directory,context);}

}
