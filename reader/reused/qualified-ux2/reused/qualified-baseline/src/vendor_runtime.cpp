// SPDX-License-Identifier: AGPL-3.0-only
// Independent interoperability adapter. No Muse/epfb-re/Quill source incorporated.
#include "vendor.h"
#include <QCoreApplication>
#include <QImage>
#include <QRect>
#include <QCryptographicHash>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include <sys/utsname.h>
#include <cerrno>
#include <cstring>
#include <limits.h>
#include <atomic>
#include <cstdio>
#include "startup_lock.h"
static_assert(sizeof(QRect)==16 && sizeof(qint64)==8);
namespace {
using Cleanup=void(*)(void*);
using Constructor=void(*)(QImage*,unsigned char*,int,int,qint64,QImage::Format,Cleanup,void*);
std::atomic<Constructor> ctor1{nullptr},ctor2{nullptr};spike::CaptureTable table;
std::atomic<bool> capture{false};std::atomic<bool> foreignCapture{false};thread_local bool captureThread=false;
void observe(unsigned char*p,int w,int h,qint64 stride,int format)noexcept{
 if(!capture.load(std::memory_order_acquire))return;
 if(!captureThread){foreignCapture=true;return;}
 table.observe({p,w,h,format,stride});
}
bool fingerprint(const char*path,const char*expected)noexcept{
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return false;
 struct stat before{},after{};bool ok=fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&before.st_uid==0&&!(before.st_mode&0022)&&before.st_size>0&&before.st_size<32*1024*1024;
 QCryptographicHash hash(QCryptographicHash::Sha256);char bytes[16384];ssize_t count;
 while(ok&&(count=read(fd,bytes,sizeof(bytes)))!=0){if(count<0){ok=false;break;}hash.addData(QByteArrayView(bytes,count));}
 ok=ok&&fstat(fd,&after)==0&&before.st_ino==after.st_ino&&before.st_size==after.st_size;
 close(fd);return ok&&hash.result().toHex()==expected;
}
bool expectedDevice()noexcept{
 struct utsname u{};if(uname(&u)||std::strcmp(u.machine,"aarch64"))return false;
 auto has=[](const char*path,const char*needle){
  // Firmware metadata may legitimately be a system-owned symlink. Verify the
  // opened regular target and complete line; vendor ELF pins still forbid links.
  int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return false;struct stat st{};
  if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&0022)){close(fd);return false;}
  char bytes[4096]{};ssize_t n=read(fd,bytes,sizeof(bytes)-1);close(fd);
  if(n<=0||n==ssize_t(sizeof(bytes)-1))return false;
  const char*match=std::strstr(bytes,needle);if(!match)return false;
  char end=match[std::strlen(needle)];return (match==bytes||match[-1]=='\n')&&(end=='\n'||end=='\0');
 };
 return has("/sys/devices/soc0/machine","reMarkable Ferrari")&&has("/etc/os-release","IMG_VERSION=\"3.27.3.0\"")&&has("/etc/version","20260612085811");
}
bool libraryOrigin(const void*symbol,const char*expected)noexcept{
 Dl_info info{};char canonical[PATH_MAX]{};return dladdr(symbol,&info)&&info.dli_fname&&realpath(info.dli_fname,canonical)&&std::strcmp(canonical,expected)==0;
}
class Runtime final:public spike::VendorCalls {
 using Instance=void*(*)();using Swap=void(*)(void*,QRect,int,int,int);
 void* library_{};void* singleton_{};Swap swap_{};bool attempted_{};QCoreApplication* application_{};
public:
 bool acquire(spike::Pixels&out)noexcept override{
  if(attempted_)return false;attempted_=true;
  if(!expectedDevice()){std::fprintf(stderr,"DIRECT_VENDOR_REJECT gate=device_metadata_or_lock\n");return false;}
  if(QCoreApplication::instance()||std::getenv("LD_PRELOAD")||std::getenv("LD_LIBRARY_PATH")||std::getenv("LD_AUDIT")||std::getenv("QTFB_KEY")){std::fprintf(stderr,"DIRECT_VENDOR_REJECT gate=existing_application_or_loader_environment\n");return false;}
  struct Identity{const char*path;const char*hash;};
  const Identity ids[]={
   {"/usr/lib/plugins/scenegraph/libqsgepaper.so","3800e9f3d01f40fa3f5cc96ca49bcd982471ce5b033320f8788707520c8ddd08"},
   {"/usr/lib/libQt6Core.so.6.8.2","71003668f8d8a787e02aa104999c90318a6cc295023e3b7a31eae721b2dd67b1"},
   {"/usr/lib/libQt6Gui.so.6.8.2","cdc0c0a7af21ba25666dc59401baa0ba826f2664c9733ff1b8a0b3fc778aada5"},
   {"/usr/lib/libQt6Quick.so.6.8.2","3d5f775d6c7bed313bc7dfdf2152c1f84cbd1820582ee0d9d1e320fc222585a4"},
   {"/usr/lib/libQt6Qml.so.6.8.2","69ddb3a3cbdcd276ffb092b11909dfc98de7f1df92f002872cdde6557a3e2d67"}};
  for(const auto&id:ids)if(!fingerprint(id.path,id.hash)){std::fprintf(stderr,"DIRECT_VENDOR_REJECT gate=fingerprint path=%s\n",id.path);return false;}
  struct utsname local{};QFile bootFile(QStringLiteral("/proc/sys/kernel/random/boot_id"));
  if(uname(&local)||!bootFile.open(QIODevice::ReadOnly))return false;
  const auto bootId=bootFile.read(65).trimmed();bootFile.close();
  if(bootId.size()!=36||!spike::prepareDisplayLock(QStringLiteral("/tmp/epframebuffer.lock"),QByteArray(local.nodename),bootId)){
   std::fprintf(stderr,"DIRECT_VENDOR_REJECT gate=lock_owner_live_or_unproven\n");return false;
  }
  // Resolve forwarding BEFORE any capture or singleton call; no per-event dlsym.
  ctor1=reinterpret_cast<Constructor>(dlsym(RTLD_NEXT,"_ZN6QImageC1EPhiixNS_6FormatEPFvPvES2_"));
  ctor2=reinterpret_cast<Constructor>(dlsym(RTLD_NEXT,"_ZN6QImageC2EPhiixNS_6FormatEPFvPvES2_"));
  if(!ctor1||!ctor2||!libraryOrigin(&QImage::staticMetaObject,ids[2].path)||!libraryOrigin(reinterpret_cast<void*>(ctor1.load()),ids[2].path)||!libraryOrigin(reinterpret_cast<void*>(&qVersion),ids[1].path)){std::fprintf(stderr,"DIRECT_VENDOR_REJECT gate=qt_symbol_origin\n");return false;}
  if(void*existing=dlopen(ids[0].path,RTLD_NOW|RTLD_NOLOAD)){dlclose(existing);return false;}
  static int argc=1;static char arg0[]="inkbound-direct-handwriting-spike2";static char*argv[]={arg0,nullptr};
  application_=new(std::nothrow)QCoreApplication(argc,argv);if(!application_)return false;
  library_=dlopen(ids[0].path,RTLD_NOW|RTLD_GLOBAL);if(!library_){std::fprintf(stderr,"DIRECT_VENDOR_REJECT gate=dlopen error=%s\n",dlerror());return false;}
  auto instance=reinterpret_cast<Instance>(dlsym(library_,"_ZN13EPFramebuffer8instanceEv"));
  swap_=reinterpret_cast<Swap>(dlsym(library_,"_ZN13EPFramebuffer11swapBuffersE5QRect13EPContentType12EPScreenMode6QFlagsINS_10UpdateFlagEE"));
  if(!instance||!swap_)return false;
  captureThread=true;capture.store(true,std::memory_order_release);singleton_=instance();capture=false;captureThread=false;
  bool ok=singleton_&&!foreignCapture&&table.select(out);if(!ok)std::fprintf(stderr,"DIRECT_VENDOR_REJECT gate=singleton_surface_capture\n");return ok;
 }
 void region(spike::Rect r,int content,int mode,int flags)noexcept override{swap_(singleton_,QRect(r.x0,r.y0,r.x1-r.x0,r.y1-r.y0),content,mode,flags);}
 void events()noexcept override{QCoreApplication::processEvents(QEventLoop::AllEvents,1);}
 // Intentionally no dlclose/vendor destructor/Qt deletion: process teardown owns
 // them. Shutdown can block, so only the qualified external owner may restore.
};
}
extern "C" __attribute__((visibility("default"))) void inkbound_c1(QImage*,unsigned char*,int,int,qint64,QImage::Format,Cleanup,void*) asm("_ZN6QImageC1EPhiixNS_6FormatEPFvPvES2_");
extern "C" void inkbound_c1(QImage*self,unsigned char*p,int w,int h,qint64 s,QImage::Format f,Cleanup c,void*i){
 Constructor original=ctor1.load(std::memory_order_acquire);
 if(!original){original=reinterpret_cast<Constructor>(dlsym(RTLD_NEXT,"_ZN6QImageC1EPhiixNS_6FormatEPFvPvES2_"));ctor1.store(original,std::memory_order_release);}
 if(!original)std::abort();original(self,p,w,h,s,f,c,i);observe(p,w,h,s,int(f));
}
extern "C" __attribute__((visibility("default"))) void inkbound_c2(QImage*,unsigned char*,int,int,qint64,QImage::Format,Cleanup,void*) asm("_ZN6QImageC2EPhiixNS_6FormatEPFvPvES2_");
extern "C" void inkbound_c2(QImage*self,unsigned char*p,int w,int h,qint64 s,QImage::Format f,Cleanup c,void*i){
 Constructor original=ctor2.load(std::memory_order_acquire);
 if(!original){original=reinterpret_cast<Constructor>(dlsym(RTLD_NEXT,"_ZN6QImageC2EPhiixNS_6FormatEPFvPvES2_"));ctor2.store(original,std::memory_order_release);}
 if(!original)std::abort();original(self,p,w,h,s,f,c,i);observe(p,w,h,s,int(f));
}
namespace spike {VendorCalls& realVendorCalls()noexcept{static Runtime runtime;return runtime;}}
