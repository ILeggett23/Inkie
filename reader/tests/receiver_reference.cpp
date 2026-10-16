// Host/local-VM integration harness using the production client and real parser.
// Test-only loopback TLS origin seam is not exposed by the shipping helper CLI.
#include "receiver.h"
#include "import_inspector.h"
#include "underlay.h"
#include "ux_shell.h"
#include <thread>
#include <chrono>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>
#include <sys/stat.h>
class LostAckTransport final:public companion::HttpTransport {
 companion::HttpTransport& inner_;bool enabled_,dropped_{};
public:LostAckTransport(companion::HttpTransport& inner,bool enabled):inner_(inner),enabled_(enabled){}
 companion::HttpResponse request(const QByteArray& method,const QString& path,const QByteArray& body,const QMap<QByteArray,QByteArray>& headers,qint64 max,int timeout)override{auto response=inner_.request(method,path,body,headers,max,timeout);if(enabled_&&!dropped_&&method=="POST"&&path.endsWith("/ack")){dropped_=true;throw std::runtime_error("simulated response loss after real server ACK");}return response;}
};
int main(int argc,char** argv){QCoreApplication app(argc,argv);umask(0077);if(argc<5)return 64;
 try{std::filesystem::path root=argv[1];QFile pem(argv[3]);if(!pem.open(QIODevice::ReadOnly))return 65;QSslCertificate ca(pem.readAll());companion::VerifiedHttpTransport http(companion::VerifiedHttpTransport::ForTests{},QUrl(QString::fromLocal8Bit(argv[2])),ca);QString action=QString::fromLocal8Bit(argv[4]);LostAckTransport wire(http,action=="sync-drop-ack");companion::Receiver receiver(root,wire,library::inspectDocument);
  if(action=="pair")receiver.startPairing("Synthetic production reader");else if(action=="poll")receiver.pollPairing();else if(action=="approve")receiver.approvePairing(true);else if(action=="reject")receiver.approvePairing(false);else if(action=="sync"||action=="sync-drop-ack")receiver.syncOnce();else if(action=="retry")receiver.retryTransfer();else if(action=="delete"&&argc==6){library::Store store(root);store.trash(QString::fromLocal8Bit(argv[5]),true);store.permanentlyDelete(QString::fromLocal8Bit(argv[5]),true);}else if(action=="open-all"&&argc==6){auto books=library::documents(root);for(int index=0;index<books.size();++index){ux::Shell shell(argv[5],root,true,false);if(!shell.choose(unsigned(index)))throw std::runtime_error("reference-book-selection");auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);while(!shell.writable()){shell.tick();if(shell.takeChange().full)shell.installed();if(shell.failed()||std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("reference-book-open");std::this_thread::sleep_for(std::chrono::milliseconds(1));}if(!shell.finish(5000)||shell.unsaved())throw std::runtime_error("reference-book-drain");}}
  else if(action!="status"&&action!="list")return 64;
  auto report=receiver.status();QJsonArray books;for(auto& d:library::documents(root))books.append(QJsonObject{{"id",d.id},{"title",d.title},{"format",d.format},{"pages",d.pages},{"sha256",d.sha256}});report["books"]=books;
  std::cout<<QJsonDocument(report).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<"REFERENCE_RECEIVER_FAILED "<<e.what()<<'\n';return 1;}
}
