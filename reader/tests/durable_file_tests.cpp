#include "durable_file.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
using namespace reader::storage;
namespace fs=std::filesystem;
static void check(bool b){if(!b)throw std::runtime_error("check failed");}
static std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
static void save(const fs::path& p,const std::string& s){replaceDurably(p,std::span<const char>(s));}
int main(){
 char dir[]="/tmp/inkbound-durability-XXXXXX";check(mkdtemp(dir));fs::path root=fs::canonical(dir),path=root/"state";
 try{
  save(path,"old");check(read(path)=="old");
  struct stat st{};check(stat(path.c_str(),&st)==0&&(st.st_mode&0777)==0600);
  // Prior algorithm's exact pre-rename crash residue must not block any save.
  {std::ofstream f(path.string()+".tmp");f<<"interrupted-old-save";}
  {std::ofstream f(root/".inkbound-pending-abandoned");f<<"recoverable";}
  save(path,"new");check(read(path)=="new"&&read(path.string()+".tmp")=="interrupted-old-save");
  check(read(root/".inkbound-pending-abandoned")=="recoverable");
  unsigned failures=0;
  for(auto fault:{Fault::Write,Fault::FileSync,Fault::BeforeRename}) {
   injectFault(fault);try{save(path,"bad");}catch(const std::exception&){++failures;}
   check(read(path)=="new");
  }
  check(failures==3);injectFault(Fault::DirectorySync);bool uncertain=false;
  try{save(path,"renamed-but-not-confirmed");}catch(const std::exception&){uncertain=true;}
  check(uncertain&&read(path)=="renamed-but-not-confirmed");injectFault(Fault::None);
  save(path,"reconciled");check(read(path)=="reconciled");
  auto unsafe=root/"unsafe";fs::create_directory(unsafe);chmod(unsafe.c_str(),0777);
  bool denied=false;try{save(unsafe/"state","secret");}catch(const std::exception&){denied=true;}check(denied&&!fs::exists(unsafe/"state"));
  auto link=root/"alias";fs::create_directory_symlink(root,link);denied=false;
  try{save(link/"state","secret");}catch(const std::exception&){denied=true;}check(denied&&read(path)=="reconciled");
  fs::create_directory(root/"child");denied=false;
  try{save(link/"child"/"state","secret");}catch(const std::exception&){denied=true;}check(denied&&!fs::exists(root/"child"/"state"));
  unsigned temporaries=0;for(const auto& e:fs::directory_iterator(root))if(e.path().filename().string().starts_with(".inkbound-pending-"))++temporaries;
  check(temporaries==1);
  // Reclaiming interrupted writes: only exact, private, single-link temporaries not kept.
  {fs::path d=root/"reclaim";fs::create_directory(d);chmod(d.c_str(),0700);
   auto make=[&](const std::string& n,mode_t m){auto p=d/n;{std::ofstream f(p);f<<"x";}chmod(p.c_str(),m);return p;};
   const std::string a=".inkbound-pending-0123456789abcdef0123456789abcdef",b=".inkbound-pending-fedcba9876543210fedcba9876543210",c=".inkbound-pending-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",e=".inkbound-pending-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",g=".inkbound-pending-cccccccccccccccccccccccccccccccc";
   make(a,0600);make(b,0600);make(c,0640);make(e,0600);fs::create_hard_link(d/e,d/"other-link");make(".inkbound-pending-abandoned",0600);make(".inkbound-pending-0123456789ABCDEF0123456789ABCDEF",0600);make("prepared.bin",0600);
   fs::create_symlink(d/"prepared.bin",d/g);
   int fd=open(d.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);check(fd>=0);
   check(interruptedWrites(fd).size()==5); // a, b, c, e and the symlink g; never non-hex or uppercase names
   const std::string keep[]{b};check(reclaimInterruptedWrites(fd,keep)==1);close(fd);
   check(!fs::exists(d/a)&&fs::exists(d/b)&&fs::exists(d/c)&&fs::exists(d/e)&&fs::is_symlink(d/g)&&fs::exists(d/"prepared.bin"));
   check(fs::exists(d/".inkbound-pending-abandoned")&&fs::exists(d/".inkbound-pending-0123456789ABCDEF0123456789ABCDEF"));}
  fs::remove_all(root);
  std::cout<<"DURABLE_FILE_TESTS_PASS cases=12\n";
 }catch(...){fs::remove_all(root);throw;}
}
