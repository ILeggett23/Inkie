#include "underlay.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QCryptographicHash>
#include <openssl/evp.h>
#include <dlfcn.h>
#include <cmath>
#include <fstream>
#include <functional>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
namespace {std::atomic<unsigned> evpCalls{0};bool forceEVPFailure{};}
extern "C" int EVP_Digest(const void* data,size_t length,unsigned char* out,unsigned int* size,const EVP_MD* type,ENGINE* implementation){++evpCalls;if(forceEVPFailure)return 0;using Original=int(*)(const void*,size_t,unsigned char*,unsigned int*,const EVP_MD*,ENGINE*);static auto original=reinterpret_cast<Original>(dlsym(RTLD_NEXT,"EVP_Digest"));if(!original)std::abort();return original(data,length,out,size,type,implementation);}
using namespace underlay;
#define CHECK(v) do{if(!(v))throw std::runtime_error(#v);}while(0)
static spike::Surface surface(std::vector<std::uint32_t>& p){return {p,Width,Height,Stride};}
static std::unique_ptr<Operation> op(std::uint64_t id,bool erase,int x,int y,int ex,int ey){auto p=std::make_unique<Operation>();p->id=id;p->eraser=erase;p->count=3;p->points[0]={x,y,spike::Phase::Down,1,erase};p->points[1]={ex,ey,spike::Phase::Move,2,erase};p->points[2]={ex,ey,spike::Phase::Up,3,erase};return p;}
static void paint(std::vector<std::uint32_t>& ink,const Operation& op){for(std::size_t i=0;i<op.count;++i)spike::segment(surface(ink),i?op.points[i-1]:op.points[i],op.points[i],op.eraser?4:2,op.eraser?0xffffffffu:0xff000000u);}
static void send(Pipeline& p,const Operation& o){CHECK(p.reserve());for(std::size_t i=0;i<o.count;++i)CHECK(p.append(o.points[i]));}
template<class F>static void rejects(F f){bool rejected=false;try{f();}catch(...){rejected=true;}CHECK(rejected);}
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);if(argc!=3)return 64;std::filesystem::path root=argv[1],data=argv[2];std::filesystem::create_directories(data);unsigned count=0;
 auto test=[&](const char* name,std::function<void()> f){f();std::printf("PASS %s\n",name);++count;};
 try{
 auto original=readBounded(root/"fixtures/inkbound-device-test.pdf",1024*1024);
 Prepared prepared=prepare(root,data/"main");
 if(std::getenv("INKBOUND_MEASURE_TRANSPORT")){std::vector<double> saves,loads;setTransientPreparedTransport(true);for(unsigned n=0;n<20;++n){auto at=std::chrono::steady_clock::now();savePrepared(data/"transport-perf.bin",prepared);auto saved=std::chrono::steady_clock::now();auto loaded=loadPrepared(data/"transport-perf.bin");auto read=std::chrono::steady_clock::now();CHECK(loaded.background==prepared.background&&loaded.ink==prepared.ink&&loaded.metadata==prepared.metadata&&loaded.binding==prepared.binding);auto save=std::chrono::duration<double,std::milli>(saved-at).count(),load=std::chrono::duration<double,std::milli>(read-saved).count();saves.push_back(save);loads.push_back(load);std::printf("TRANSPORT_SAMPLE n=%u save_ms=%.6f load_ms=%.6f\n",n,save,load);}auto report=[](const char* phase,std::vector<double> values){std::sort(values.begin(),values.end());auto q=[&](double p){return values[std::size_t(std::ceil(values.size()*p))-1];};std::printf("TRANSPORT_PERF phase=%s n=%zu p50_ms=%.6f p95_ms=%.6f p99_ms=%.6f worst_ms=%.6f raster_bytes=%zu identical_roundtrip=true evp_calls=%u optical=false tablet=NONE\n",phase,values.size(),q(.5),q(.95),q(.99),values.back(),std::size_t(Pixels*4),evpCalls.load());};report("save",saves);report("load",loads);setTransientPreparedTransport(false);return 0;}
 test("sha256_known_vectors_differential_and_provider_fallback",[&]{CHECK(digest(QByteArray()).toHex()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");CHECK(digest("abc").toHex()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");for(int size:{0,1,63,64,65,4096,1048576,int(Pixels*4)}){QByteArray bytes(size,Qt::Uninitialized);for(int n=0;n<size;++n)bytes[n]=char(n%251);CHECK(digest(bytes)==QCryptographicHash::hash(bytes,QCryptographicHash::Sha256));}CHECK(evpCalls.load()>0);const auto before=evpCalls.load();forceEVPFailure=true;auto fallback=digest("abc");forceEVPFailure=false;CHECK(evpCalls.load()==before+1&&fallback.toHex()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");});
 test("prepared_bytes_match_retained_qt_reference",[&]{auto p=prepared;auto bg=QByteArray::fromRawData(reinterpret_cast<const char*>(p.background.data()),Pixels*4),ink=QByteArray::fromRawData(reinterpret_cast<const char*>(p.ink.data()),Pixels*4);auto qt=[](const QByteArray& bytes){return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);};p.metadata["backgroundSha256"]=QString::fromLatin1(qt(bg).toHex());p.binding=qt(QJsonDocument(p.metadata).toJson(QJsonDocument::Compact));QJsonObject envelope{{"meta",p.metadata},{"binding",QString::fromLatin1(p.binding.toHex())},{"lastId",QString::number(p.lastId)},{"journal",QString::fromLatin1(p.journalDigest.toHex())},{"inkSha256",QString::fromLatin1(qt(ink).toHex())}};auto head=QJsonDocument(envelope).toJson(QJsonDocument::Compact);unsigned length=head.size();char bytes[4]={char(length),char(length>>8),char(length>>16),char(length>>24)};auto expected=QByteArray("INKPREP1",8)+QByteArray(bytes,4)+head+qt(head)+bg+ink;savePrepared(data/"reference.bin",p);CHECK(readBounded(data/"reference.bin",32*1024*1024)==expected);for(qsizetype offset:{qsizetype(12),qsizetype(44+head.size()),qsizetype(44+head.size()+Pixels*4)}){auto corrupted=expected;corrupted[offset]^=1;atomicFile(data/"reference-corrupt.bin",corrupted);rejects([&]{loadPrepared(data/"reference-corrupt.bin");});}});
 test("actual_mupdf_fixture_render",[&]{CHECK(prepared.background.size()==Pixels);CHECK(std::count(prepared.background.begin(),prepared.background.end(),0xff000000u)>1000);CHECK(digest(original).toHex()==FixtureHash);});
 test("missing_document_rejected_before_arm",[&]{rejects([&]{prepare(data/"missing-root",data/"missing-prep");});});
 test("private_data_symlink_rejected",[&]{std::filesystem::create_directory_symlink(data,data/"alias");rejects([&]{privateDirectory(data/"alias/escape");});CHECK(!std::filesystem::exists(data/"escape"));});
 test("journal_hardlink_rejected",[&]{std::filesystem::create_hard_link(data/"main/annotations.ink",data/"hardlink.ink");rejects([&]{Journal j(data/"hardlink.ink",prepared.binding);});std::filesystem::remove(data/"hardlink.ink");});
 test("malformed_or_wrong_document_rejected",[&]{std::filesystem::create_directories(data/"bad-root/fixtures");atomicFile(data/"bad-root/fixtures/inkbound-device-test.pdf",QByteArray("%PDF-invalid"));rejects([&]{prepare(data/"bad-root",data/"bad-prep");});});
 test("invalid_prepared_geometry_rejected",[&]{auto p=prepared;p.metadata["width"]=8192;rejects([&]{savePrepared(data/"bad-geometry",p);});p=prepared;p.metadata["rotation"]=90;rejects([&]{savePrepared(data/"bad-rotation",p);});});
 test("prepared_roundtrip_and_identity",[&]{auto p=loadPrepared(data/"main/prepared.bin");CHECK(p.background==prepared.background&&p.ink==prepared.ink&&p.binding==prepared.binding);CHECK(p.metadata["pageToSurface"].toArray()[0].toDouble()==2.575);});
 test("prepared_corruption_rejected",[&]{auto bytes=readBounded(data/"main/prepared.bin",32*1024*1024);bytes[bytes.size()-20]^=1;atomicFile(data/"bad.bin",bytes);rejects([&]{loadPrepared(data/"bad.bin");});});
 auto draw=op(1,false,80,200,900,200),erase=op(2,true,300,200,600,200),redraw=op(3,false,400,195,400,205);
 auto ink=prepared.ink;paint(ink,*draw);paint(ink,*erase);paint(ink,*redraw);
 {auto expected=prepared.background;compose(surface(expected),prepared.background,surface(ink),{0,0,Width,Height});atomicFile(data/"expected-composite.sha",digest(QByteArray(reinterpret_cast<const char*>(expected.data()),expected.size()*4)).toHex());}
 test("white_pixel_reference_unchanged",[&]{std::vector<std::uint32_t> white(Pixels,0xffffffffu),out(Pixels,0xffffffffu);compose(surface(out),white,surface(ink),{0,0,Width,Height});CHECK(out==ink);});
 test("document_safe_partial_erase_and_redraw",[&]{std::vector<std::uint32_t> out(Pixels,0xffffffffu);compose(surface(out),prepared.background,surface(ink),{0,0,Width,Height});CHECK(out[200*Stride+450]==prepared.background[200*Stride+450]);CHECK(out[200*Stride+400]==0xff000000u);CHECK(out[200*Stride+100]==0xff000000u);});
 test("dirty_region_no_outside_change",[&]{std::vector<std::uint32_t> out(Pixels,0x12345678u);compose(surface(out),prepared.background,surface(ink),{298,196,605,205});CHECK(out[0]==0x12345678u&&out[196*Stride+297]==0x12345678u&&out[205*Stride+300]==0x12345678u);});
 test("document_text_and_image_restored_exactly",[&]{auto erased=prepared.ink;std::vector<std::uint32_t> out(Pixels,0xffffffffu);for(int y:{230,1700}){auto d=op(1,false,110,y,900,y);paint(erased,*d);auto e=op(2,true,110,y,900,y);paint(erased,*e);}compose(surface(out),prepared.background,surface(erased),{0,0,Width,Height});CHECK(out==prepared.background);});
 test("ordered_commit_duplicate_replay",[&]{Journal j(data/"main/annotations.ink",prepared.binding);j.append(*draw);j.append(*erase);j.append(*redraw);j.append(*erase);CHECK(j.lastId()==3);std::vector<std::uint32_t> replay(Pixels,0xffffffffu);j.replay(surface(replay));CHECK(replay==ink);});
 test("fresh_process_replay",[&]{auto pid=fork();CHECK(pid>=0);if(pid==0){try{Journal j(data/"main/annotations.ink",prepared.binding);std::vector<std::uint32_t> replay(Pixels,0xffffffffu);j.replay(surface(replay));_exit(replay==ink?0:1);}catch(...){_exit(2);}}int status=0;CHECK(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);});
 test("reprepare_replays_before_arm",[&]{auto p=prepare(root,data/"main");CHECK(p.ink==ink&&p.lastId==3&&p.background==prepared.background);});
 test("document_binding_mismatch",[&]{rejects([&]{Journal j(data/"main/annotations.ink",QByteArray(32,'x'));});});
 auto current=prepare(root,data/"main");
 test("stale_prepared_journal_denied",[&]{rejects([&]{Pipeline p(data/"main/annotations.ink",prepared);});});
 test("truncated_final_record_preserves_prior",[&]{auto bytes=readBounded(data/"main/annotations.ink",64*1024*1024);atomicFile(data/"tail.ink",bytes+QByteArray("IN",2));Journal j(data/"tail.ink",prepared.binding);std::vector<std::uint32_t> replay(Pixels,0xffffffffu);j.replay(surface(replay));CHECK(replay==ink&&j.lastId()==3);});
 test("middle_checksum_corruption_rejected",[&]{auto bytes=readBounded(data/"main/annotations.ink",64*1024*1024);bytes[60]^=1;atomicFile(data/"corrupt.ink",bytes);rejects([&]{Journal j(data/"corrupt.ink",prepared.binding);});});
 test("corrupt_middle_length_not_truncated_tail",[&]{auto bytes=readBounded(data/"main/annotations.ink",64*1024*1024);bytes[48]=0;bytes[49]=0;bytes[50]=16;bytes[51]=0;atomicFile(data/"corrupt-length.ink",bytes);rejects([&]{Journal j(data/"corrupt-length.ink",prepared.binding);});});
 test("unknown_schema_rejected",[&]{auto bytes=readBounded(data/"main/annotations.ink",64*1024*1024);bytes[7]='9';atomicFile(data/"version.ink",bytes);rejects([&]{Journal j(data/"version.ink",prepared.binding);});});
 test("conflicting_duplicate_rejected",[&]{auto changed=op(1,false,50,50,60,60);Journal j(data/"main/annotations.ink",prepared.binding);rejects([&]{j.append(*changed);});});
 test("write_failure_preserves_committed_prefix",[&]{atomicFile(data/"writefail.ink",readBounded(data/"main/annotations.ink",64*1024*1024));{Journal j(data/"writefail.ink",prepared.binding,{17,false,false,0});auto more=op(4,false,1,1,9,9);rejects([&]{j.append(*more);});}Journal j(data/"writefail.ink",prepared.binding);CHECK(j.lastId()==3);});
 test("sync_failure_not_committed",[&]{atomicFile(data/"syncfail.ink",readBounded(data/"main/annotations.ink",64*1024*1024));Journal j(data/"syncfail.ink",prepared.binding,{-1,true,false,0});auto more=op(4,false,1,1,9,9);rejects([&]{j.append(*more);});CHECK(j.lastId()==3);});
 test("directory_sync_failure",[&]{rejects([&]{Journal j(data/"dirfail.ink",prepared.binding,{-1,false,true,0});});});
 test("invalid_path_permission_error",[&]{rejects([&]{Journal j(data/"missing/annotations.ink",prepared.binding);});});
 test("stalled_writer_nonblocking_handoff_capacity",[&]{auto p=prepare(root,data/"stall");Pipeline pipeline(data/"stall/annotations.ink",p,{-1,false,false,100});auto begin=std::chrono::steady_clock::now();for(unsigned i=0;i<Slots;++i)send(pipeline,*draw);CHECK(!pipeline.reserve());auto elapsed=std::chrono::steady_clock::now()-begin;CHECK(elapsed<std::chrono::milliseconds(100));CHECK(!pipeline.finish(1));CHECK(pipeline.finish(1000));CHECK(pipeline.committed==Slots&&pipeline.highWater>=3&&pipeline.rejected==1&&pipeline.unsaved()==0);});
 test("worker_error_is_unsaved_and_denies_next_contact",[&]{auto p=prepare(root,data/"workerfail");Pipeline pipeline(data/"workerfail/annotations.ink",p,{0,false,false,0});send(pipeline,*draw);CHECK(pipeline.finish(1000));CHECK(pipeline.failed()&&pipeline.unsaved()==1&&!pipeline.reserve()&&pipeline.committed==0);});
 test("disk_full_reports_unsaved",[&]{auto p=prepare(root,data/"diskfull");Pipeline pipeline(data/"diskfull/annotations.ink",p,{0,false,false,0,ENOSPC});send(pipeline,*draw);CHECK(pipeline.finish(1000));CHECK(pipeline.failed()&&pipeline.unsaved()==1&&pipeline.committed==0);});
 test("release_endpoint_and_sealed_buffer_retained",[&]{auto p=prepare(root,data/"endpoint");auto o=op(1,false,50,50,80,50);o->points[2].x=90;{Pipeline pipeline(data/"endpoint/annotations.ink",p,{-1,false,false,30});send(pipeline,*o);o->points[2].x=200;CHECK(pipeline.finish(1000));}auto q=prepare(root,data/"endpoint");CHECK(q.ink[50*Stride+90]==0xff000000u&&q.ink[50*Stride+200]==0xffffffffu);});
 test("termination_with_queued_work_preserves_durable_prefix",[&]{auto p=prepare(root,data/"terminate");{Journal j(data/"terminate/annotations.ink",p.binding);j.append(*draw);}p=prepare(root,data/"terminate");auto pid=fork();CHECK(pid>=0);if(pid==0){Pipeline pipeline(data/"terminate/annotations.ink",p,{-1,false,false,200});send(pipeline,*erase);bool finished=pipeline.finish(1);_exit(!finished&&pipeline.unsaved()==1?0:2);}int status;CHECK(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);Journal j(data/"terminate/annotations.ink",p.binding);CHECK(j.lastId()==1);});
 test("ack_does_not_remove_visible_ink",[&]{auto p=prepare(root,data/"ack");paint(p.ink,*draw);auto snapshot=p.ink;Pipeline pipeline(data/"ack/annotations.ink",p);send(pipeline,*draw);CHECK(pipeline.finish(1000));CHECK(p.ink==snapshot);});
 test("pdf_bytes_unchanged",[&]{CHECK(readBounded(root/"fixtures/inkbound-device-test.pdf",1024*1024)==original);});
 // Render artifact for visual QA; actual prepared PDF, not a shipped screenshot.
 std::ofstream preview(data/"prepared.pgm",std::ios::binary);preview<<"P5\n"<<Width<<" "<<Height<<"\n255\n";for(int y=0;y<Height;++y)for(int x=0;x<Width;++x)preview.put(char(prepared.background[std::size_t(y)*Stride+x]&255));
 std::printf("UNDERLAY_TESTS_PASS cases=%u vendor_calls=MOCK_OR_NONE tablet_access=0\n",count);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"UNDERLAY_TEST_FAIL %s\n",e.what());return 1;}
}
