#include "tom_helper_main.h"
#include "product_store.h"
#include "receiver.h"
#include "tom_session.h"
#include "tom_store.h"
#include "underlay.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QSslSocket>
#include <optional>
#include <QStringList>
#include <thread>
#include <chrono>
#include <string_view>
#include <cstdio>
#include <csignal>
#include <unistd.h>
namespace tom {namespace {
template<class F>auto stored(const std::filesystem::path& root,F f){for(unsigned attempt=0;;++attempt){try{companion::TomStore store(root);return f(store);}catch(const std::exception& e){if(std::string_view(e.what())!="Tom store busy"||attempt>=1000)throw;std::this_thread::sleep_for(std::chrono::milliseconds(5));}}}
bool uuid(const QString& id){return !QUuid(id).isNull()&&QUuid(id).toString(QUuid::WithoutBraces)==id;}
QJsonObject configJson(const companion::TomConfig& c){QJsonArray models;for(auto model:c.models)models.append(model);return {{"configured",c.configured},{"provider",QJsonObject{{"id",c.provider},{"label",c.label}}},{"models",models},{"max_output_tokens",c.maxOutputTokens},{"daily_limit",c.dailyLimit}};}
}
// The alarm ends a stuck command; it says so on stderr first (async-signal-safe) so the journal shows a timeout,
// not a silent death.
extern "C" void tomHelperTimeout(int){static const char line[]="TOM_COMMAND_TIMEOUT\n";(void)!write(2,line,sizeof line-1);_exit(78);}
int helperMain(int argc,char** argv){struct sigaction timeout{};timeout.sa_handler=tomHelperTimeout;sigemptyset(&timeout.sa_mask);if(sigaction(SIGALRM,&timeout,nullptr))return 78;alarm(35);const auto started=std::chrono::steady_clock::now();if(argc!=5&&argc!=7)return 78;auto action=QString::fromLocal8Bit(argv[2]),nonce=QString::fromLocal8Bit(argv[4]);std::filesystem::path root=argv[3];if(!root.is_absolute()||!uuid(nonce)||!QStringList{"config","create","resume","cancel","ink-create","ink-poll","ink-cancel"}.contains(action))return 78;QString conversation,turn;if(action!="config"){if(argc!=7)return 78;conversation=QString::fromLocal8Bit(argv[5]);turn=QString::fromLocal8Bit(argv[6]);if(!uuid(conversation)||!uuid(turn))return 78;}
 QJsonObject result{{"ok",false}};std::unique_ptr<companion::Receiver> retainedReceiver;
 try{if(!QSslSocket::supportsSsl())throw std::runtime_error("TLS unavailable");companion::VerifiedHttpTransport http;retainedReceiver=std::make_unique<companion::Receiver>(root,http,[](const std::filesystem::path&,const QString&)->library::Inspection{throw std::runtime_error("Tom cannot import");},action.startsWith("ink-")?companion::ReceiverUse::Visual:companion::ReceiverUse::Interactive);auto& receiver=*retainedReceiver;
  if(action=="config"){auto device=receiver.status()["device_id"].toString();auto config=receiver.fetchTomConfig(device);result={{"ok",true},{"config",configJson(config)},{"device_id",device},{"read_answer",config.readAnswer},{"illustration_model",config.illustrationModel},{"ink_plan",config.inkPlan},{"auto_answer",config.autoAnswer},{"read_auto_answer",config.readAutoAnswer},{"scene_answer",config.sceneAnswer},{"read_scene_answer",config.readSceneAnswer},{"scene_max_tokens",config.sceneMaxTokens},{"scene_version",config.sceneVersion},{"mixed_reply",config.mixedReply},{"memory",config.memory}};}
  else if(action.startsWith("ink-")){auto file=root/"tom-ink-cache/requests"/(conversation.toStdString()+".json");auto request=QJsonDocument::fromJson(underlay::readBounded(file,262144)).object();product::require(request["request_id"]==conversation&&conversation==turn,"Tom illustration request binding");auto device=receiver.status()["device_id"].toString();if(action=="ink-cancel"){receiver.cancelIllustration(conversation,device);result={{"ok",true},{"status","canceled"}};}else{auto state=action=="ink-create"?receiver.createIllustration(request,device):receiver.illustrationStatus(conversation,device);result={{"ok",true},{"status",state["status"]},{"request_id",conversation}};if(state["status"]=="completed"){auto image=receiver.illustrationBurst(conversation,device);if(image.isEmpty()){result["status"]="downloading";}else{auto directory=root/"tom-ink-cache/images";underlay::privateDirectory(directory);underlay::atomicFile(directory/(conversation.toStdString()+".png"),image);result["image_sha256"]=QString::fromLatin1(underlay::digest(image).toHex());}}}}
  else{
   if(action=="create"){std::optional<companion::TomCreateAttempt> attempt;attempt.emplace(stored(root,[&](auto& store){return store.markCreateAttempt(conversation,turn);}));auto ack=receiver.createTom(attempt->request(),attempt->deviceId());stored(root,[&](auto& store){store.confirmCreate(conversation,turn,ack);});}
   auto state=companion::TomStore::readTurn(root,conversation,turn);auto request=state["request_id"].toString(),device=state["device_id"].toString();companion::TomSession session(receiver);session.resume(request,device,quint64(state["cursor"].toInt()));
   if(action=="cancel"){stored(root,[&](auto& store){store.markCancelAttempt(conversation,turn);});session.cancel();}
   auto remote=session.requestStatus();stored(root,[&](auto& store){store.recordStatus(conversation,turn,remote);});
   state=companion::TomStore::readTurn(root,conversation,turn);// The stream gets the time left of a 24 s command budget (alarm 35 s, parent deadline 40 s), so a slow answer ends this command
   // cleanly with what has arrived instead of the alarm killing it mid-stream; the runtime resumes it straight away.
   const int budget=24000-int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count());
   bool streamDone=true;if(budget>=2000&&state["status"]!="completed"&&state["status"]!="canceled"&&state["status"]!="interrupted")streamDone=session.streamOnce([&](const companion::TomEvent& event){
    for(unsigned attempt=0;;++attempt){try{companion::TomStore store(root);return store.applyEvent(conversation,turn,request,event);}catch(const std::exception& e){if(std::string_view(e.what())!="Tom store busy"||attempt>=1000)throw;std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
   },budget);if(!streamDone)std::fputs("TOM_STREAM_PAUSED\n",stderr);
   result={{"ok",true},{"turn",companion::TomStore::readTurn(root,conversation,turn)}};
  }
 }catch(const std::exception& e){if(retainedReceiver)try{result["retry_after_ms"]=retainedReceiver->retryAfterMs();}catch(...){}result["error"]="Tom unavailable; saved draft and response retained";
  // The reason goes to the journal (never into the store or the UI): the internal error code only, printable ASCII.
  {std::string reason(e.what());if(reason.size()>120)reason.resize(120);for(auto& ch:reason)if(ch<32||ch>126)ch='?';std::fprintf(stderr,"TOM_COMMAND_ERROR action=%s reason=%s\n",action.toLatin1().constData(),reason.c_str());}if(action!="config"&&!action.startsWith("ink-"))try{stored(root,[&](auto& store){store.recordFailure(conversation,turn,dynamic_cast<const companion::TomRequestUnavailable*>(&e)?"request-unavailable":action=="cancel"?"cancel-uncertain":"network");});result["turn"]=companion::TomStore::readTurn(root,conversation,turn);}catch(...){} }
 try{auto directory=root/"tom/results";underlay::privateDirectory(directory);underlay::atomicFile(directory/(nonce.toStdString()+".json"),QJsonDocument(QJsonObject{{"command_id",nonce},{"result",result}}).toJson(QJsonDocument::Compact));std::puts("TOM_COMMAND_FINISHED");return 0;}catch(...){std::fputs("TOM_COMMAND_FAILED\n",stderr);return 78;}
}
}
