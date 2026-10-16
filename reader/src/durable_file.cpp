#include "durable_file.h"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <random>
#include <cstdio>
#include <dirent.h>
#include <string_view>
#include <algorithm>
namespace reader::storage {
namespace {
#ifdef INKBOUND_DURABILITY_TEST_FAULTS
thread_local Fault fault=Fault::None;
bool fails(Fault f) {return fault==f;}
#endif
void require(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
struct File {
 int fd{-1};
 ~File(){if(fd>=0)close(fd);}
};
}
#ifdef INKBOUND_DURABILITY_TEST_FAULTS
void injectFault(Fault f){fault=f;}
#endif
namespace {
constexpr std::string_view PendingPrefix=".inkbound-pending-";
bool pendingName(const std::string& n) {
 if(n.size()!=PendingPrefix.size()+32||!n.starts_with(PendingPrefix))return false;
 return std::all_of(n.begin()+PendingPrefix.size(),n.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
}
std::vector<std::string> interruptedWrites(int directory) {
 int copy=openat(directory,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
 require(copy>=0,"interrupted-write-list-open");
 DIR* stream=fdopendir(copy);
 if(!stream){close(copy);require(false,"interrupted-write-list-stream");}
 std::vector<std::string> out;unsigned seen=0;errno=0;
 while(auto* e=readdir(stream)) {
  if(++seen>65536){closedir(stream);require(false,"interrupted-write-list-capacity");}
  std::string name=e->d_name;if(pendingName(name))out.push_back(std::move(name));
 }
 const bool failed=errno!=0;closedir(stream);require(!failed,"interrupted-write-list-read");
 return out;
}
unsigned reclaimInterruptedWrites(int directory,std::span<const std::string> keep) {
 unsigned removed=0;
 for(const auto& name:interruptedWrites(directory)) {
  if(std::find(keep.begin(),keep.end(),name)!=keep.end())continue;
  struct stat st{};
  if(fstatat(directory,name.c_str(),&st,AT_SYMLINK_NOFOLLOW)!=0){require(errno==ENOENT,"interrupted-write-inspect");continue;}
  if(!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1||(st.st_mode&0077))continue; // not ours to remove; inspection fails closed
  require(unlinkat(directory,name.c_str(),0)==0||errno==ENOENT,"interrupted-write-reclaim");++removed;
 }
 if(removed)require(fsync(directory)==0,"interrupted-write-reclaim-sync");
 return removed;
}
static void replace(const std::filesystem::path& path,std::span<const char> data,bool durable,const std::function<void(int)>& producer={}) {
 require(path.is_absolute()&&!path.filename().empty(),"atomic-path");
 File directory{open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
 require(directory.fd>=0,"atomic-directory-open");
 for(const auto& part:path.parent_path().relative_path()) {
  auto name=part.string();require(!name.empty()&&name!="."&&name!="..","atomic-directory-component");
  int next=openat(directory.fd,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  require(next>=0,"atomic-directory-open");close(directory.fd);directory.fd=next;
 }
 auto destination=path.filename().string();
 require(destination!="."&&destination!="..","atomic-filename");
 struct stat st{};
 require(fstat(directory.fd,&st)==0&&st.st_uid==getuid()&&!(st.st_mode&0022),"atomic-directory-owner-mode");
 // Bind all operations to the checked parent inode, even if it is renamed.
 // Private O_EXCL names avoid deleting or overwriting interrupted predecessors.
 std::random_device random;
 std::string name;File temporary;
 for(unsigned attempt=0;attempt<8;++attempt) {
  char suffix[33];std::snprintf(suffix,sizeof(suffix),"%08x%08x%08x%08x",random(),random(),random(),random());
  name=".inkbound-pending-"+std::string(suffix);
  temporary.fd=openat(directory.fd,name.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
  if(temporary.fd>=0)break;
  if(errno!=EEXIST)break;
 }
 require(temporary.fd>=0,"atomic-temporary-open");
 bool renamed=false;
 try {
  require(fcntl(temporary.fd,F_SETFD,FD_CLOEXEC)==0&&fchmod(temporary.fd,0600)==0,"atomic-temporary-mode");
  if(producer){
#ifdef INKBOUND_DURABILITY_TEST_FAULTS
   require(!fails(Fault::Write),"atomic-write-injected");
#endif
   producer(temporary.fd);
  }else{
  std::size_t offset=0;
  while(offset<data.size()) {
#ifdef INKBOUND_DURABILITY_TEST_FAULTS
   require(!fails(Fault::Write),"atomic-write-injected");
#endif
   auto n=write(temporary.fd,data.data()+offset,data.size()-offset);
   if(n<0&&errno==EINTR)continue;
   require(n>0,"atomic-write");offset+=static_cast<std::size_t>(n);
  }
  }
#ifdef INKBOUND_DURABILITY_TEST_FAULTS
  require(!fails(Fault::FileSync),"atomic-file-sync-injected");
#endif
  require(!durable||fsync(temporary.fd)==0,"atomic-file-sync");
#ifdef INKBOUND_DURABILITY_TEST_FAULTS
  require(!fails(Fault::BeforeRename),"atomic-before-rename-injected");
#endif
  require(renameat(directory.fd,name.c_str(),directory.fd,destination.c_str())==0,"atomic-rename");renamed=true;
#ifdef INKBOUND_DURABILITY_TEST_FAULTS
  require(!fails(Fault::DirectorySync),"atomic-directory-sync-injected");
#endif
  require(!durable||fsync(directory.fd)==0,"atomic-directory-sync");
 }catch(...) {
  if(!renamed)unlinkat(directory.fd,name.c_str(),0);
  throw;
 }
}
void replaceDurably(const std::filesystem::path& path,std::span<const char> data){replace(path,data,true);}
void replaceDurablyGenerated(const std::filesystem::path& path,const std::function<void(int)>& producer){require(bool(producer),"atomic-producer");replace(path,{},true,producer);}
void replaceTransient(const std::filesystem::path& path,std::span<const char> data){replace(path,data,false);}
}
