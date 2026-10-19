#include "epub_typography.h"
#include "underlay.h"
#include "library.h"
#include "durable_file.h"
#include <QCryptographicHash>
#include <openssl/evp.h>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <bit>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <cmath>
#include "pdf_viewport.h"
namespace underlay {
namespace {
constexpr std::size_t MaxJournal=64*1024*1024,MaxRecord=1024*1024;
bool injected(bool value){
#ifdef UNDERLAY_TEST_FAULTS
 return value;
#else
 (void)value;return false;
#endif
}
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
QByteArray json(const QJsonObject& o){return QJsonDocument(o).toJson(QJsonDocument::Compact);}
QJsonObject object(const QByteArray& b){QJsonParseError e;auto d=QJsonDocument::fromJson(b,&e);require(e.error==QJsonParseError::NoError&&d.isObject(),"invalid-json");return d.object();}
void allWrite(int fd,const char* p,std::size_t n,IoFault* fault=nullptr){
 (void)fault;
 std::size_t off=0;while(off<n){
#ifdef UNDERLAY_TEST_FAULTS
  if(fault&&fault->failWriteAfter==0){errno=fault->writeErrno;throw std::runtime_error("injected-write-error");}
  std::size_t take=n-off;if(fault&&fault->failWriteAfter>0)take=std::min(take,std::size_t(fault->failWriteAfter));
#else
  std::size_t take=n-off;
#endif
  auto k=write(fd,p+off,take);if(k<0&&errno==EINTR)continue;require(k>0,"write-error");off+=std::size_t(k);
#ifdef UNDERLAY_TEST_FAULTS
  if(fault&&fault->failWriteAfter>0)fault->failWriteAfter-=int(k);
#endif
 }
}
void syncDir(const std::filesystem::path& p,bool fail=false){int fd=open(p.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(fd>=0,"directory-open");bool ok=!fail&&fsync(fd)==0;close(fd);require(ok,"directory-sync");}
QByteArray pixels(const std::vector<std::uint32_t>& p){return QByteArray::fromRawData(reinterpret_cast<const char*>(p.data()),qsizetype(p.size()*4));}
std::uint32_t length(const char* p){return std::uint32_t(static_cast<unsigned char>(p[0]))|(std::uint32_t(static_cast<unsigned char>(p[1]))<<8)|(std::uint32_t(static_cast<unsigned char>(p[2]))<<16)|(std::uint32_t(static_cast<unsigned char>(p[3]))<<24);}
QByteArray frame(const QByteArray& b){std::uint32_t n=b.size();char len[4]={char(n),char(n>>8),char(n>>16),char(n>>24)};return QByteArray(len,4)+b+digest(b);}
void validateMeta(const QJsonObject& m){require(!m.contains("legacyMeta")||m["render"]=="reader-fit-footer100-mupdf1.28.3-rgb32-v2","legacy-color-receipt-context");if(m["render"]=="reader-fit-footer100-mupdf1.28.3-rgb32-v2"){auto legacy=m["legacyMeta"].toObject();require(legacy["schema"]==2&&legacy["render"]=="reader-fit-footer100-mupdf1.28.3-threshold192-v1","color-legacy-render");validateMeta(legacy);auto expected=legacy;expected["render"]=m["render"];expected["backgroundSha256"]=m["backgroundSha256"];expected["legacyMeta"]=legacy;require(expected==m&&library::validHash(legacy["backgroundSha256"].toString())&&library::validHash(m["backgroundSha256"].toString()),"color-source-geometry-binding");}int pages=m["pages"].toInt(),index=m["page"].toInt(-1);bool view=m["schema"].toInt()==3;bool epubView=m["schema"].toInt()==4;bool imported=m["schema"].toInt()==2||view||epubView;require((m["schema"].toInt()==1||imported)&&pages>0&&pages<=1000&&index>=0&&index<pages&&m["generation"].toInt()==1&&m["width"].toInt()==Width&&m["height"].toInt()==Height&&m["stride"].toInt()==Stride*4&&(imported?(library::validHash(m["pdf"].toString())&&library::validId(m["document"].toString())&&!m["layout"].toString().isEmpty()&&(m["documentFormat"]=="pdf"||m["documentFormat"]=="epub")):m["pdf"].toString()==FixtureHash)&&m["format"].toString()=="RGB32-LE"&&m["raster"].toString()=="qualified-circle-bresenham-v1"&&(view?(m["documentFormat"]=="pdf"&&library::validHash(m["view_id"].toString())&&m["render"]=="reader-pdf-view-mupdf1.28.3-rgb32-v1"):epubView?(m["documentFormat"]=="epub"&&library::validHash(m["view_id"].toString())&&m["render"]=="reader-epub-view-mupdf1.28.3-rgb32-v1"):(m["render"].toString()=="reader-fit-footer100-mupdf1.28.3-threshold192-v1"||(m["schema"]==2&&m["render"]=="reader-fit-footer100-mupdf1.28.3-rgb32-v2"))),"prepared-identity");
 auto b=m["bounds"].toArray(),c=m["crop"].toArray(),t=m["pageToSurface"].toArray(),inv=m["surfaceToPage"].toArray();if(view){ /* orientation (docs/LANDSCAPE.md): absent for portrait; 90/270 for a page shown turned, part of its identity and plan */
  const bool turned=m.contains("orientation");const int orientation=turned?m["orientation"].toInt(-1):0;require(!turned||(m["orientation"].isDouble()&&(orientation==90||orientation==270)),"view-orientation");
  QJsonObject identity{{"version",1},{"document",m["document"]},{"source_sha",m["pdf"]},{"page",index},{"bounds",m["bounds"]},{"view",m["view_settings"]}};if(turned)identity["orientation"]=orientation;
  require(m["page"].isDouble()&&m["page"].toDouble()==index&&m["view_id"].toString().toLatin1()==digest(json(identity)).toHex(),"view-config-identity");require(b.size()==4&&c.size()==4&&t.size()==6&&inv.size()==6&&m["view_settings"].isObject(),"view-geometry");for(auto v:b)require(v.isDouble()&&std::isfinite(v.toDouble()),"view-bounds-value");auto settings=m["view_settings"].toObject();require(settings.size()==4&&settings["zoom"].isDouble()&&settings["rotation"].isDouble()&&settings["rotation"].toDouble()==settings["rotation"].toInt(-1)&&settings["pan"].isArray()&&settings["pan"].toArray().size()==2,"view-settings");reader::pdfview::View state;state.zoom=settings["zoom"].toDouble();state.rotation=unsigned(settings["rotation"].toInt(-1));auto pan=settings["pan"].toArray();for(auto v:pan)require(v.isDouble()&&std::isfinite(v.toDouble()),"view-pan");state.pan={pan[0].toDouble(),pan[1].toDouble()};if(!settings["crop"].isNull()){auto crop=settings["crop"].toArray();require(crop.size()==4,"view-crop");for(auto v:crop)require(v.isDouble()&&std::isfinite(v.toDouble()),"view-crop-value");state.crop=reader::pdfview::Rect{crop[0].toDouble(),crop[1].toDouble(),crop[2].toDouble(),crop[3].toDouble()};}auto plan=reader::pdfview::plan({b[0].toDouble(),b[1].toDouble(),b[2].toDouble(),b[3].toDouble()},state,unsigned(orientation));require(plan.scale>=.1&&plan.scale<=16&&m["rotation"].toInt(-1)==int(state.rotation),"view-ink-geometry");const double crops[]{plan.crop.x,plan.crop.y,plan.crop.width,plan.crop.height};for(int i=0;i<4;++i)require(c[i].isDouble()&&std::abs(c[i].toDouble()-crops[i])<1e-9,"view-crop-binding");for(int i=0;i<6;++i){require(t[i].isDouble()&&inv[i].isDouble()&&std::isfinite(t[i].toDouble())&&std::isfinite(inv[i].toDouble())&&std::abs(t[i].toDouble())<=100000&&std::abs(t[i].toDouble()-plan.forward.value[i])<1e-9&&std::abs(inv[i].toDouble()-plan.inverse.value[i])<1e-9,"view-transform-binding");}return;}
if(epubView){
 auto receipt=m["view_receipt"].toObject(),layout=receipt["layout"].toObject();
 auto integer=[](const QJsonValue& v,int lo,int hi){return v.isDouble()&&v.toDouble()==v.toInt(lo-1)&&v.toInt()>=lo&&v.toInt()<=hi;};
 require(receipt.size()==9&&receipt["version"]==1&&receipt["document"]==m["document"]&&receipt["source_sha"]==m["pdf"]&&receipt["format"]=="epub"&&integer(receipt["base_pages"],1,1000)&&receipt["base_layout"].isString()&&!receipt["base_layout"].toString().isEmpty()&&receipt["base_layout"].toString().size()<=256&&receipt["pages"]==m["pages"]&&integer(m["pages"],1,1000)&&integer(m["page"],0,pages-1)&&receipt["layout_id"]==m["layout"]&&digest(json(receipt)).toHex()==m["view_id"].toString().toLatin1(),"epub-view-receipt-binding");
 require(layout.size()==8&&layout["engine"]=="MuPDF"&&layout["version"]=="1.28.3"&&integer(layout["width"],64,4094)&&integer(layout["height"],64,4094)&&integer(layout["fontSize"],6,72)&&layout["publisherCss"].isBool()&&layout["publisherCss"].toBool()&&layout["userCss"].isString()&&epub::valid(layout["userCss"].toString())&&layout["textPolicy"]=="collapse-whitespace-v1"&&receipt["layout_id"].toString()=="epub-reflow-v1:"+QString::fromLatin1(digest(json(layout)).toHex()),"epub-view-layout-binding");
 require(b.size()==4&&c==b&&t.size()==6&&inv.size()==6,"epub-view-geometry");for(auto v:b)require(v.isDouble()&&std::isfinite(v.toDouble()),"epub-view-bounds");
 double x=b[0].toDouble(),y=b[1].toDouble(),w=b[2].toDouble(),h=b[3].toDouble();require(w>0&&h>0,"epub-view-bounds-size");
 // Turned (docs/LANDSCAPE.md): orientation 90/270, the page fitted upright into the landscape viewport with exactly
 // the renderer's float coefficients (pdfview::fitTurned) and their inverse; absent for portrait pages.
 if(m.contains("orientation")){const int orientation=m["orientation"].toInt(-1);require(m["orientation"].isDouble()&&(orientation==90||orientation==270)&&m["rotation"].toInt(-1)==0,"epub-view-orientation");const auto f=reader::pdfview::fitTurned(x,y,w,h,unsigned(orientation));
  for(int i=0;i<6;++i)require(t[i].isDouble()&&t[i].toDouble()==double(f[i]),"epub-view-turn-binding");const double fa=f[0],fb=f[1],fc=f[2],fd=f[3],fe=f[4],ff=f[5],det=fa*fd-fb*fc;require(std::isfinite(det)&&std::abs(det)>0,"epub-view-turn-scale");
  const double inverse[]{fd/det,-fb/det,-fc/det,fa/det,(fc*ff-fd*fe)/det,(fb*fe-fa*ff)/det};for(int i=0;i<6;++i)require(inv[i].isDouble()&&std::abs(inv[i].toDouble()-inverse[i])<1e-9,"epub-view-turn-inverse");return;}
 double fit=std::min(1620/w,2060/h);require(fit>=.1&&fit<=16,"epub-view-fit");double scale=float(fit),tx=float((1620-w*fit)/2-x*fit),ty=float((2060-h*fit)/2-y*fit);const double expected[]{scale,0,0,scale,tx,ty};for(int i=0;i<6;++i)require(t[i].isDouble()&&std::isfinite(t[i].toDouble())&&std::abs(t[i].toDouble())<=100000&&t[i].toDouble()==expected[i],"epub-view-fit-binding");
}
require(m["rotation"].toInt(-1)==0&&b.size()==4&&c==b&&b[2].toDouble()>0&&b[3].toDouble()>0&&t.size()==6&&inv.size()==6,"page-geometry");
 for(auto v:t)require(v.isDouble()&&std::isfinite(v.toDouble())&&std::abs(v.toDouble())<=1000000,"transform-value");for(auto v:inv)require(v.isDouble()&&std::isfinite(v.toDouble()),"inverse-value");
 auto scale=t[0].toDouble();require(scale>0&&scale<=16&&t[3].toDouble()==scale&&t[1].toDouble()==0&&t[2].toDouble()==0&&std::abs(inv[0].toDouble()*scale-1)<1e-9&&inv[3]==inv[0]&&inv[1].toDouble()==0&&inv[2].toDouble()==0&&std::abs(inv[4].toDouble()+t[4].toDouble()/scale)<1e-9&&std::abs(inv[5].toDouble()+t[5].toDouble()/scale)<1e-9,"prepared-transform");}
}
QByteArray digest(const QByteArray& b){unsigned char bytes[EVP_MAX_MD_SIZE]{};unsigned size=0;/* SHA-256 and every validation stay identical; provider failure uses the retained reference implementation. */if(EVP_Digest(b.constData(),std::size_t(b.size()),bytes,&size,EVP_sha256(),nullptr)==1&&size==32)return QByteArray(reinterpret_cast<const char*>(bytes),32);return QCryptographicHash::hash(b,QCryptographicHash::Sha256);}
QByteArray readBounded(const std::filesystem::path& p,std::size_t limit){
 int fd=open(p.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);require(fd>=0,"read-open");struct stat st{};
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||std::uint64_t(st.st_size)>limit){close(fd);throw std::runtime_error("read-size-type");}
 QByteArray out(st.st_size,'\0');std::size_t n=0;while(n<std::size_t(out.size())){auto k=read(fd,out.data()+n,out.size()-n);if(k<0&&errno==EINTR)continue;if(k<=0){close(fd);throw std::runtime_error("short-read");}n+=k;}close(fd);return out;
}
void atomicFile(const std::filesystem::path& path,const QByteArray& data){
 reader::storage::replaceDurably(path,{data.constData(),static_cast<std::size_t>(data.size())});
}

void privateDirectory(const std::filesystem::path& path){
 require(path.is_absolute(),"data-path-not-absolute");int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);require(fd>=0,"data-root-open");
 try{for(const auto& part:path.relative_path()){auto name=part.string();require(name!="."&&name!=".."&&!name.empty(),"data-path-component");int next=openat(fd,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
   if(next<0&&errno==ENOENT){require(mkdirat(fd,name.c_str(),0700)==0,"data-mkdir");require(fsync(fd)==0,"data-parent-sync");next=openat(fd,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);}
   require(next>=0,"data-path-symlink-or-open");close(fd);fd=next;
  }struct stat st{};require(fstat(fd,&st)==0&&st.st_uid==getuid()&&!(st.st_mode&0022),"data-directory-owner-mode");require(fchmod(fd,0700)==0&&fsync(fd)==0,"data-directory-sync");close(fd);
 }catch(...){close(fd);throw;}
}
void compose(spike::Surface out,std::span<const std::uint32_t> bg,spike::Surface ink,spike::Rect r){
 require(out.valid()&&ink.valid()&&out.width==ink.width&&out.height==ink.height&&out.stridePixels==ink.stridePixels&&bg.size()==out.pixels.size(),"compose-surface");
 require(r.x0>=0&&r.y0>=0&&r.x1<=out.width&&r.y1<=out.height&&!r.empty(),"compose-region");
 for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x){auto i=std::size_t(y)*out.stridePixels+x;out.pixels[i]=ink.pixels[i]==0xff000000u?0xff000000u:bg[i];}
}
namespace {bool transientPrepared=false;}
void setTransientPreparedTransport(bool on)noexcept{transientPrepared=on;}
QByteArray bindingDigest(const QJsonObject& m){return digest(json(m.contains("legacyMeta")?m["legacyMeta"].toObject():m));}
QByteArray preparedBinding(const QJsonObject& m){validateMeta(m);return bindingDigest(m);}
void savePrepared(const std::filesystem::path& path,Prepared& p){
 require(p.background.size()==Pixels&&p.ink.size()==Pixels,"prepared-size");validateMeta(p.metadata);
 p.metadata["backgroundSha256"]=QString::fromLatin1(digest(pixels(p.background)).toHex());p.binding=preparedBinding(p.metadata);
 QJsonObject envelope{{"meta",p.metadata},{"binding",QString::fromLatin1(p.binding.toHex())},{"lastId",QString::number(p.lastId)},{"journal",QString::fromLatin1(p.journalDigest.toHex())},{"inkSha256",QString::fromLatin1(digest(pixels(p.ink)).toHex())}};
 auto head=json(envelope);require(head.size()<=4096,"prepared-header");{QByteArray bytes;bytes.reserve(qsizetype(44+head.size()+Pixels*8));bytes.append("INKPREP1",8);bytes.append(frame(head));bytes.append(pixels(p.background));bytes.append(pixels(p.ink));if(transientPrepared)reader::storage::replaceTransient(path,{bytes.constData(),std::size_t(bytes.size())});else atomicFile(path,bytes);}
}
Prepared loadPrepared(const std::filesystem::path& path){
 require(std::endian::native==std::endian::little,"unsupported-endian");auto raw=readBounded(path,8+4+4096+32+Pixels*8);
 require(raw.size()>=44&&raw.first(8)=="INKPREP1","prepared-magic");auto n=length(raw.data()+8);require(n<=4096&&raw.size()==qsizetype(44+n+Pixels*8),"prepared-byte-count");auto head=raw.mid(12,n);require(digest(head)==raw.mid(12+n,32),"prepared-header-integrity");auto e=object(head);Prepared p;p.metadata=e["meta"].toObject();validateMeta(p.metadata);p.binding=preparedBinding(p.metadata);require(p.binding.toHex()==e["binding"].toString().toLatin1(),"prepared-binding");bool ok=false;p.lastId=e["lastId"].toString().toULongLong(&ok);require(ok&&p.lastId<=MaxOperations,"prepared-order");p.journalDigest=QByteArray::fromHex(e["journal"].toString().toLatin1());require(p.journalDigest.size()==32,"prepared-journal");p.background.resize(Pixels);p.ink.resize(Pixels);std::memcpy(p.background.data(),raw.data()+44+n,Pixels*4);std::memcpy(p.ink.data(),raw.data()+44+n+Pixels*4,Pixels*4);
 require(digest(pixels(p.background)).toHex()==p.metadata["backgroundSha256"].toString().toLatin1()&&digest(pixels(p.ink)).toHex()==e["inkSha256"].toString().toLatin1(),"prepared-pixel-integrity");
 const bool color=p.metadata["schema"]==3||p.metadata["schema"]==4||p.metadata["render"]=="reader-fit-footer100-mupdf1.28.3-rgb32-v2";for(auto c:p.background)require(color?(c>>24)==255:(c==0xffffffffu||c==0xff000000u),"background-format");for(auto c:p.ink)require(c==0xffffffffu||c==0xff000000u,"coverage-format");return p;
}
Journal::Journal(const std::filesystem::path& path,const QByteArray& binding,IoFault fault):path_(path),binding_(binding),fault_(fault){
 require(binding.size()==32,"journal-binding-size");fd_=open(path.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);require(fd_>=0,"journal-open");
 try{struct stat st{};require(fstat(fd_,&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_nlink==1&&!(st.st_mode&0077),"journal-owner-mode");require(flock(fd_,LOCK_EX|LOCK_NB)==0,"journal-already-open");if(st.st_size==0){auto h=QByteArray("INKANN01",8)+binding_;allWrite(fd_,h.data(),h.size(),&fault_);require(!injected(fault_.failSync)&&fsync(fd_)==0,"journal-header-sync");syncDir(path.parent_path(),injected(fault_.failDirectorySync));}load(nullptr);require(fsync(fd_)==0,"journal-recovery-sync");}
 catch(...){close(fd_);fd_=-1;throw;}
}
Journal::~Journal(){if(fd_>=0)close(fd_);}
void Journal::checkPath()const{struct stat fd{},path{};require(fstat(fd_,&fd)==0&&lstat(path_.c_str(),&path)==0&&S_ISREG(path.st_mode)&&fd.st_dev==path.st_dev&&fd.st_ino==path.st_ino&&fd.st_nlink==1&&path.st_uid==getuid()&&!(path.st_mode&0077),"journal-path-identity");}
QByteArray Journal::fingerprint()const{checkPath();auto bytes=readBounded(path_,MaxJournal);checkPath();return digest(bytes);}
void Journal::load(spike::Surface* target){
 checkPath();
 auto raw=readBounded(path_,MaxJournal);require(raw.size()>=40&&raw.first(8)=="INKANN01"&&raw.mid(8,32)==binding_,"journal-document-or-schema");last_=0;offset_=40;hashes_.clear();unsigned records=0;
 while(offset_<std::size_t(raw.size())){
  auto remain=std::size_t(raw.size())-offset_;
  if(remain<8){require(QByteArray("INKOP001",8).startsWith(raw.mid(offset_)),"corrupt-tail-magic");break;}
  require(raw.mid(offset_,8)=="INKOP001","record-magic");if(remain<12)break;
  auto n=length(raw.data()+offset_+8);require(n>0&&n<=MaxRecord,"record-size");
  if(remain<std::size_t(n)+44){require(raw.indexOf("INKOP001",offset_+8)<0,"corrupt-middle-length");break;}
  auto body=raw.mid(offset_+12,n);auto hash=raw.mid(offset_+12+n,32);require(digest(body)==hash,"record-checksum");auto o=object(body);bool ok=false;auto id=o["id"].toString().toULongLong(&ok);require(ok&&id&&id<=MaxOperations&&o["version"].toInt()==1&&o["binding"].toString().toLatin1()==binding_.toHex(),"record-identity");require(++records<=2*MaxOperations,"record-count");
  if(id<=last_){require(hashes_[id-1]==hash,"conflicting-duplicate");offset_+=n+44;continue;}
  require(id==last_+1,"record-order");auto tool=o["tool"].toString();bool erase=tool=="eraser";require(erase||tool=="pen","record-tool");require(o["radius"].toInt()==(erase?4:2),"record-radius");auto pts=o["points"].toArray();require(pts.size()>=2&&pts.size()<=int(MaxPoints),"record-points");spike::Point previous{};
  for(int i=0;i<pts.size();++i){auto a=pts[i].toArray();require(a.size()==3,"record-point");auto integral=[](const QJsonValue& v){return v.isDouble()&&v.toDouble()==v.toInt(-999999);};require(integral(a[0])&&integral(a[1])&&integral(a[2]),"record-coordinate-type");int x=a[0].toInt(),y=a[1].toInt(),phase=a[2].toInt();require(x>=0&&x<Width&&y>=0&&y<Height&&phase==(i==0?0:i==pts.size()-1?2:1),"record-path");spike::Point p{x,y,static_cast<spike::Phase>(phase),std::uint64_t(i+1),erase};if(target)spike::segment(*target,i?previous:p,p,erase?4:2,erase?0xffffffffu:0xff000000u);previous=p;}
  hashes_.push_back(hash);last_=id;offset_+=n+44;
 }
 if(offset_!=std::size_t(raw.size())){require(ftruncate(fd_,offset_)==0&&fsync(fd_)==0,"tail-repair-failed");}
 require(lseek(fd_,offset_,SEEK_SET)>=0,"journal-seek");
}
void Journal::replay(spike::Surface surface){require(surface.valid()&&surface.width==Width&&surface.height==Height&&surface.stridePixels==Stride,"replay-surface");load(&surface);}
void Journal::append(const Operation& op){
 checkPath();
 require(op.count>=2&&op.count<=MaxPoints&&op.id&&op.id<=MaxOperations,"operation-bound");QJsonArray points;
 for(std::size_t i=0;i<op.count;++i){const auto& p=op.points[i];require(p.x>=0&&p.x<Width&&p.y>=0&&p.y<Height&&p.eraser==op.eraser&&p.phase==(i==0?spike::Phase::Down:i==op.count-1?spike::Phase::Up:spike::Phase::Move),"operation-path");points.append(QJsonArray{p.x,p.y,int(p.phase)});}
 QJsonObject o{{"version",1},{"binding",QString::fromLatin1(binding_.toHex())},{"id",QString::number(op.id)},{"tool",op.eraser?"eraser":"pen"},{"radius",op.eraser?4:2},{"points",points}};auto body=json(o);auto hash=digest(body);if(op.id<=last_){require(hashes_[op.id-1]==hash,"duplicate-conflict");return;}require(op.id==last_+1,"append-order");auto bytes=QByteArray("INKOP001",8)+frame(body);require(body.size()<=int(MaxRecord)&&offset_+bytes.size()<=MaxJournal,"journal-capacity");allWrite(fd_,bytes.data(),bytes.size(),&fault_);require(!injected(fault_.failSync)&&fsync(fd_)==0,"operation-sync");offset_+=bytes.size();last_=op.id;hashes_.push_back(hash);
}
Pipeline::Pipeline(const std::filesystem::path& path,const Prepared& p,IoFault fault):slots_(std::make_unique<std::array<Operation,Slots>>()),journal_(std::make_unique<Journal>(path,p.binding,fault)),next_(p.lastId+1),fault_(fault){
 require(journal_->lastId()==p.lastId&&journal_->fingerprint()==p.journalDigest,"stale-preparation");worker_=std::thread([this]{work();});
}
Pipeline::~Pipeline(){if(!finish())std::_Exit(78);}
bool Pipeline::canReserve()const noexcept{
 if(active_>=0||stop_.load()||failed_.load(std::memory_order_acquire)||next_>MaxOperations)return false;
 for(unsigned i=0;i<Slots;++i){auto state=(*slots_)[i].state.load(std::memory_order_acquire);if(state==SaveState::Free||state==SaveState::Committed)return true;}
 return false;
}
bool Pipeline::reserve()noexcept{
 if(active_>=0||stop_.load()||failed_.load(std::memory_order_acquire)||next_>MaxOperations){++rejected;return false;}
 for(unsigned i=0;i<Slots;++i){auto state=(*slots_)[i].state.load(std::memory_order_acquire);if(state==SaveState::Free||state==SaveState::Committed){auto& op=(*slots_)[i];op.id=next_++;op.count=0;op.state.store(SaveState::VisibleVolatile,std::memory_order_release);active_=int(i);return true;}}
 ++rejected;return false;
}
bool Pipeline::append(const spike::Point& p)noexcept{
 if(active_<0)return false;auto& op=(*slots_)[active_];if(op.count==MaxPoints){cancel();return false;}if(!op.count)op.eraser=p.eraser;op.points[op.count++]=p;
 if(p.phase==spike::Phase::Up){unsigned i=active_;active_=-1;op.state.store(SaveState::Queued,std::memory_order_release);auto n=queued.fetch_add(1)+1;auto old=highWater.load();while(n>old&&!highWater.compare_exchange_weak(old,n)){};if(!queue_.push(i)){op.state=SaveState::Failed;failed_=true;return false;}}
 return true;
}
void Pipeline::cancel()noexcept{if(active_>=0){(*slots_)[active_].state=SaveState::Failed;active_=-1;}}
void Pipeline::work()noexcept{
 unsigned i;while(!stop_.load(std::memory_order_acquire)||queued.load()){
  if(!queue_.pop(i)){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
  auto& op=(*slots_)[i];try{if(failed_)throw std::runtime_error("writer-stopped");
#ifdef UNDERLAY_TEST_FAULTS
   if(fault_.delayMs)std::this_thread::sleep_for(std::chrono::milliseconds(fault_.delayMs));
#endif
   journal_->append(op);--queued;++committed;op.state.store(SaveState::Committed,std::memory_order_release);}catch(...){--queued;failed_=true;op.state.store(SaveState::Failed,std::memory_order_release);}
 }done_.store(true,std::memory_order_release);
}
unsigned Pipeline::unsaved()const noexcept{unsigned n=0;for(auto& op:*slots_){auto s=op.state.load(std::memory_order_acquire);if(s==SaveState::VisibleVolatile||s==SaveState::Queued||s==SaveState::Failed)++n;}return n;}
bool Pipeline::finish(unsigned milliseconds)noexcept{
 if(!worker_.joinable())return true;stop_=true;auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
 while(!done_.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 if(!done_)return false;worker_.join();return true;
}
ComposedDisplay::ComposedDisplay(spike::VendorDisplay& v,Prepared& p,Pipeline& pipe):vendor_(v),prepared_(p),pipe_(pipe){}
bool ComposedDisplay::initialize(const spike::RecoveryLease& lease)noexcept{if(!vendor_.initialize(lease))return false;auto s=vendor_.surface();return s.width==Width&&s.height==Height&&s.stridePixels==Stride&&prepared_.background.size()==Pixels&&prepared_.ink.size()==Pixels;}
spike::Surface ComposedDisplay::surface()const noexcept{return {prepared_.ink,Width,Height,Stride};}
bool ComposedDisplay::initialPage()noexcept{try{auto out=vendor_.surface();compose(out,prepared_.background,surface(),{0,0,Width,Height});return vendor_.initialPreparedPage(out.pixels);}catch(...){return false;}}
spike::Submit ComposedDisplay::submit(spike::Rect r)noexcept{try{compose(vendor_.surface(),prepared_.background,surface(),r);return vendor_.submit(r);}catch(...){return spike::Submit::Failed;}}
void ComposedDisplay::pump()noexcept{vendor_.pump();}void ComposedDisplay::shutdown()noexcept{vendor_.shutdown();}
}
