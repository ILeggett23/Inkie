#include "reader.h"
#include "page_server.h"
#include "e2_provisional_v2.h"
#include "document_view_store.h"
#include "epub_view_store.h"
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include "durable_file.h"
namespace reader {
// Runs only on the low-priority preparer. The helper remains in this session's
// PID1 cgroup, never touches input/display, and cannot outlive cgroup drain.
underlay::Prepared prepareViewPage(const std::filesystem::path& app,const std::filesystem::path& data,int page,const std::atomic<bool>& stop,const QString& view,unsigned orientation){
 if(view.isEmpty()&&!orientation)return preparePage(app,data,page,stop);auto format=library::descriptor(data)["format"].toString();bool epub=format=="epub";QJsonObject receipt;QString expected;int expectedPages{};QString expectedLayout;
 if(orientation&&((format!="pdf"&&!(format=="epub"&&!view.isEmpty()))||(orientation!=pdfview::Clockwise&&orientation!=pdfview::Anticlockwise)))throw std::runtime_error("page-helper-turn"); /* PDF pages, and a landscape EPUB layout's pages (docs/LANDSCAPE.md) */
 if(epub){auto set=epubviews::readSet(data,view);receipt=set.receipt;expected=view;expectedPages=receipt["pages"].toInt();expectedLayout=receipt["layout_id"].toString();}
 else if(format=="pdf"){auto set=viewstore::readSet(data,view);receipt=set.receipt;expected=orientation?QString():viewstore::pageConfigId(set,page); /* turned: from the page's bounds, checked below */expectedPages=receipt["base_pages"].toInt();expectedLayout=receipt["base_layout"].toString();}
 else throw std::runtime_error("page-helper-document-format");
 if(page<0||page>=expectedPages)throw std::runtime_error("page-helper-view-page-range");
 auto helper=(app/"bin/reader-experience1-prepare").string();struct stat st{};
 if(lstat(helper.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0022))throw std::runtime_error("page-helper-identity");
 auto file=pageStoragePath(data,page,view,orientation)/"prepared.bin",temporary=file;temporary+=".tmp";
 for(const auto& p:{file,temporary}){if(lstat(p.c_str(),&st)==0||errno!=ENOENT)throw std::runtime_error("stale-transport-needs-reconciliation");}
 // Interrupted temporaries already beside the transport belong to an earlier writer and stay put;
 // only ones this helper leaves behind are reclaimed after waitpid proves it exited.
 std::vector<std::string> before;{int d=open(file.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(d>=0){try{before=storage::interruptedWrites(d);}catch(...){close(d);throw;}close(d);}else if(errno!=ENOENT)throw std::runtime_error("transport-inspection-failed");}
 std::vector<std::string> args{epub?std::string("--epub-view-page"):std::string("--view-page"),data.string(),std::to_string(page),app.string(),view.toStdString()};if(orientation)args.push_back(std::to_string(orientation));
 const bool ok=runPagePreparation(helper,args,stop); /* the persistent page server, or a one-shot helper */
 if(!ok){
  // Only files absent before this helper and created in its exact private page
  // path can be reconciled after waitpid proves this helper has exited. Journals
  // and owner reservations are never touched. Unexpected type/owner stays put.
  for(const auto& p:{file,temporary}){if(lstat(p.c_str(),&st)==0){if(!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1||(st.st_mode&0022))throw std::runtime_error("unsafe-transport-retained");if(unlink(p.c_str()))throw std::runtime_error("transport-cleanup-failed");}else if(errno!=ENOENT)throw std::runtime_error("transport-inspection-failed");}
  // A helper stopped while committing leaves its durable-write temporary. Remove it, so an
  // interrupted page leaves nothing behind and later strict inspections are not refused.
  {int d=open(file.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(d>=0){try{storage::reclaimInterruptedWrites(d,before);}catch(...){close(d);throw;}close(d);}else if(errno!=ENOENT)throw std::runtime_error("transport-inspection-failed");}
  throw std::runtime_error("page-helper-failed");
 }
 recovery::v2::e2::mark("reader:helper-exit",unsigned(page));
 auto prepared=underlay::loadPrepared(file);
 recovery::v2::e2::mark("reader:prepared-loaded",unsigned(page));
 if(orientation&&prepared.metadata["orientation"].toInt(-1)!=int(orientation))throw std::runtime_error("page-helper-result-identity");
 if(orientation&&!epub){auto set=viewstore::readSet(data,view);expected=viewstore::turnedConfigId(set,page,prepared.metadata["bounds"].toArray(),orientation);if(prepared.metadata["view_settings"].toObject()!=viewstore::viewSettings(set,page))throw std::runtime_error("page-helper-result-identity");}
 if(prepared.metadata["page"].toInt(-1)!=page||prepared.metadata.value("view_id").toString()!=expected||prepared.metadata["document"]!=receipt["document"]||prepared.metadata["pdf"]!=receipt["source_sha"]||prepared.metadata["documentFormat"]!=format||prepared.metadata["pages"]!=expectedPages||prepared.metadata["layout"]!=expectedLayout||(epub&&(prepared.metadata["schema"]!=4||prepared.metadata["view_receipt"].toObject()!=receipt)))throw std::runtime_error("page-helper-result-identity");
 std::filesystem::remove(file);return prepared;
}
}
