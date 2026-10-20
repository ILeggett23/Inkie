#include "companion_client.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
using namespace companion;
void check(bool ok){if(!ok)throw std::runtime_error("test failure");}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::runtime_error&){rejected=true;}check(rejected);}
QByteArray json(const QJsonObject& o){return QJsonDocument(o).toJson(QJsonDocument::Compact);}
int main(){try{
 const QString id="00000000-0000-0000-0000-000000000001";
 QJsonObject job{{"job_id",id},{"document_id",id},{"filename","book.pdf"},{"format","pdf"},{"sha256",QString(64,'a')},{"size",100},{"converted",false},{"intentional_new",false},{"download_token","ticket"},{"download_expires_at",200}};
 auto manifest=[&](QJsonObject j){return json({{"version",1},{"jobs",QJsonArray{j}}});};
 auto newline=job;newline["job_id"]=id+"\n";rejects([&]{parseManifest(manifest(newline),100);});newline=job;newline["sha256"]=QString(64,'a')+"\n";rejects([&]{parseManifest(manifest(newline),100);});
 auto deliveries=parseManifest(manifest(job),100);check(deliveries.size()==1&&!deliveries[0].intentionalNew);
 auto bad=job;bad.remove("intentional_new");rejects([&]{parseManifest(manifest(bad),100);});
 bad=job;bad["size"]=1.5;rejects([&]{parseManifest(manifest(bad),100);});
 bad=job;bad["intentional_new"]="true";rejects([&]{parseManifest(manifest(bad),100);});
 rejects([&]{parseManifest(manifest(job),200);});
 bad=job;bad["extra"]=true;rejects([&]{parseManifest(manifest(bad),100);});
 rejects([&]{parseManifest(json({{"version",1},{"jobs",QJsonArray{job,job}}}),100);});
 bad=job;bad["intentional_new"]=true;check(parseManifest(manifest(bad),100)[0].intentionalNew);
 bad=job;bad["size"]=double(MaxFileBytes);check(parseManifest(manifest(bad),100)[0].size==MaxFileBytes);
 bad["size"]=double(MaxFileBytes+1);rejects([&]{parseManifest(manifest(bad),100);});
 bad=job;bad["download_token"]="ticket\r\nInjected: yes";rejects([&]{parseManifest(manifest(bad),100);});
 const auto& d=deliveries[0];auto etag=QByteArray("\"")+QByteArray(64,'a')+"\"";
 validateRangeReply(d,10,19,206,"bytes 10-19/100",etag,10);
 validateRangeReply(d,0,99,200,{},etag,100);
 rejects([&]{validateRangeReply(d,10,19,200,{},etag,100);});
 rejects([&]{validateRangeReply(d,10,19,206,"bytes 11-20/100",etag,10);});
 rejects([&]{validateRangeReply(d,10,19,206,"bytes 10-19/100",etag,9);});
 rejects([&]{validateRangeReply(d,10,19,206,"bytes 10-19/100","wrong",10);});
 QJsonObject config{{"configured",true},{"provider",QJsonObject{{"id","workers-ai"},{"label","Workers AI"}}},{"models",QJsonArray{"model-id"}},{"max_output_tokens",128},{"daily_limit",30}};
 auto c=parseTomConfig(json(config));
 auto tampered=c;tampered.maxOutputTokens=2048;
 QJsonObject draft{{"request_id",id},{"conversation_id",id},{"provider","workers-ai"},{"model","model-id"},{"max_output_tokens",128},{"question","Explain"},{"scope","general"},{"context",QJsonValue::Null},{"history",QJsonArray{}},{"consent",true}};
 FrozenTomRequest frozen(c,draft,true,false);auto original=frozen.body();check(frozen.acceptsConfig(c));draft["question"]="Changed draft";check(frozen.body()==original);FrozenTomRequest changed(c,draft,true,false);check(frozen.fingerprint()!=changed.fingerprint());
 config["max_output_tokens"]=256;check(!frozen.acceptsConfig(parseTomConfig(json(config))));
 rejects([&]{FrozenTomRequest(c,draft,false,false);});
 /* Inkie's notes: only when the service takes them, never with a transcription, bounded on their own (outside the 32000) */
 {auto withMemory=c;withMemory.memory=true;auto m=draft;m["memory_capable"]=true;m["memory"]=QJsonArray{"The reader's name is Sam."};rejects([&]{FrozenTomRequest(c,m,true,false);});FrozenTomRequest kept(withMemory,m,true,false);check(kept.acceptsConfig(withMemory)&&!kept.acceptsConfig(c));
  auto onlyCapable=m;onlyCapable.remove("memory");FrozenTomRequest(withMemory,onlyCapable,true,false);auto notCapable=m;notCapable.remove("memory_capable");rejects([&]{FrozenTomRequest(withMemory,notCapable,true,false);});
  auto many=m;QJsonArray sixtyOne;for(int i=0;i<61;++i)sixtyOne.append(QString("Note %1").arg(i));many["memory"]=sixtyOne;rejects([&]{FrozenTomRequest(withMemory,many,true,false);});
  auto longNote=m;longNote["memory"]=QJsonArray{QString(241,'x')};rejects([&]{FrozenTomRequest(withMemory,longNote,true,false);});auto blank=m;blank["memory"]=QJsonArray{" "};rejects([&]{FrozenTomRequest(withMemory,blank,true,false);});
  auto tooMuch=m;QJsonArray eighteen;for(int i=0;i<18;++i)eighteen.append(QString(240,QChar('a'+i)));tooMuch["memory"]=eighteen; /* 4320 > 4096 */rejects([&]{FrozenTomRequest(withMemory,tooMuch,true,false);});
  auto full=m;full["question"]=QString(8000,'q');FrozenTomRequest(withMemory,full,true,false);}
 rejects([&]{FrozenTomRequest(tampered,draft,true,false);});
 auto invalid=draft;invalid["provider"]="openai";rejects([&]{FrozenTomRequest(c,invalid,true,false);});
 invalid=draft;invalid["max_output_tokens"]=2048;rejects([&]{FrozenTomRequest(c,invalid,true,false);});
 invalid=draft;invalid["page_image"]="data:image/png;base64,aA==";rejects([&]{FrozenTomRequest(c,invalid,true,true);});
 invalid=draft;invalid["handwriting_image"]="https://example.test/x.png";rejects([&]{FrozenTomRequest(c,invalid,true,true);});
 invalid=draft;invalid["purpose"]="transcribe";rejects([&]{FrozenTomRequest(c,invalid,true,false);});
 invalid=draft;invalid["question"]=QString(8001,'a');rejects([&]{FrozenTomRequest(c,invalid,true,false);});
 invalid=draft;invalid["history"]=QJsonArray{QJsonObject{{"role","user"},{"text","Prior selected document text"}}};rejects([&]{FrozenTomRequest(c,invalid,true,false);});
 QJsonObject binding{{"document_id",id},{"revision","revision-1"},{"anchor",QJsonObject{{"page",1}}},{"text","selected text"}};
 for(const auto& scope:QStringList{"word","sentences","paragraphs","page"}){
  auto selected=draft;selected["scope"]=scope;auto ctx=binding;
  if(scope=="page")ctx["page_kind"]="pdf-source-page";
  selected["context"]=ctx;FrozenTomRequest request(c,selected,true,false);check(!request.body().isEmpty());
  if(scope=="page"){
   ctx["page_kind"]="epub-rendered-page";selected["context"]=ctx;FrozenTomRequest epub(c,selected,true,false);check(epub.fingerprint()!=request.fingerprint());
   ctx["page_kind"]="epub-source-page";selected["context"]=ctx;rejects([&]{FrozenTomRequest(c,selected,true,false);});
   ctx.remove("page_kind");selected["context"]=ctx;rejects([&]{FrozenTomRequest(c,selected,true,false);});
   ctx["page_kind"]="pdf-source-page";
  }
  for(const auto& key:QStringList{"document_id","revision","anchor"}){
   auto missing=ctx;missing.remove(key);selected["context"]=missing;rejects([&]{FrozenTomRequest(c,selected,true,false);});
   missing=ctx;missing[key]=QJsonValue::Null;selected["context"]=missing;rejects([&]{FrozenTomRequest(c,selected,true,false);});
  }
  selected["context"]=QJsonValue::Null;rejects([&]{FrozenTomRequest(c,selected,true,false);});
 }
 // Signature-valid fixture exercises contract validation only, not image decoding.
 invalid=draft;invalid["handwriting_image"]="data:image/png;base64,iVBORw0KGgo=";
 rejects([&]{FrozenTomRequest(c,invalid,true,false);});
 FrozenTomRequest disclosed(c,invalid,true,true);check(!disclosed.body().isEmpty());
 invalid["purpose"]="transcribe";FrozenTomRequest transcription(c,invalid,true,true);check(transcription.fingerprint()!=disclosed.fingerprint());
 // read-answer: the handwriting is the whole question (no typed text), and it is required.
 invalid["purpose"]="read-answer";rejects([&]{FrozenTomRequest(c,invalid,true,true);});invalid["question"]="  ";FrozenTomRequest reading(c,invalid,true,true);check(reading.fingerprint()!=transcription.fingerprint());rejects([&]{FrozenTomRequest(c,invalid,true,false);});invalid.remove("handwriting_image");rejects([&]{FrozenTomRequest(c,invalid,true,true);});
 std::cout<<"companion contract tests passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
