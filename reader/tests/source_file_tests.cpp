#include "source_file.h"
#include "durable_file.h"
#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
using namespace reader;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>static void rejected(F f,const char* why){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,why);}
static void save(const fs::path& p,std::string_view bytes){storage::replaceDurably(p,{bytes.data(),bytes.size()});}
static std::string small(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
static long peak(){rusage usage{};check(!getrusage(RUSAGE_SELF,&usage),"peak-memory");return usage.ru_maxrss;}
int main(){char name[]="/tmp/inkbound-source-test-XXXXXX";check(mkdtemp(name),"temporary-directory");fs::path root=fs::canonical(name);
 try{
  auto input=root/"input",output=root/"output";save(input,"abc");save(output,"previous");
  auto checked=source::hashFile(input);check(checked.digest.toHex()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","known-sha");
  source::copyFile(input,output,checked);check(small(output)=="abc","copy-small");
  save(input,"");check(source::hashFile(input).digest.toHex()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","empty-sha");
  save(input,"abc");rejected([&]{source::hashFile(input,2);},"size-cap");
  fs::create_symlink(input,root/"link");rejected([&]{source::hashFile(root/"link");},"reject-symlink");
  check(!mkfifo((root/"fifo").c_str(),0600),"make-fifo");rejected([&]{source::hashFile(root/"fifo");},"reject-fifo-without-blocking");
  rejected([&]{source::hashFile(root);},"reject-directory");
  // Larger than the old whole-book limit, without constructing the book in memory.
  auto large=root/"large";std::array<char,256*1024> block;for(std::size_t i=0;i<block.size();++i)block[i]=char(i%256);
  {std::ofstream f(large,std::ios::binary);for(int i=0;i<264;++i)f.write(block.data(),block.size());check(bool(f),"write-large-fixture");}
  const auto before=peak();std::uint64_t bytes=0,largest=0;auto progress=[&](std::uint64_t n){bytes+=n;largest=std::max(largest,n);};
  auto big=source::hashFile(large,source::MaxDocumentBytes,progress);check(big.identity.size==66ULL*1024*1024&&bytes==big.identity.size&&largest<=block.size(),"large-progress-bounds");
  // Reference computed independently with Python hashlib/OpenSSL over the fixture pattern.
  check(big.digest.toHex()=="cbbe5ec0c8deced3f098868f8030c20807e53acd716291e50b5f106ab22eaab1","large-independent-sha");
  bytes=0;source::copyFile(large,output,big,progress);check(bytes==big.identity.size&&source::hashFile(output).digest==big.digest,"large-copy-identity");
  check(peak()-before<16*1024,"whole-source-allocation-regression"); // Linux ru_maxrss is KiB.
  struct stat mode{};check(!stat(output.c_str(),&mode)&&(mode.st_mode&0777)==0600,"copy-private-mode");
  save(output,"previous");save(input,"replacement");rejected([&]{source::copyFile(input,output,checked);},"identity-before-copy");check(small(output)=="previous","prior-output-retained");
  bool changed=false;rejected([&]{source::hashFile(large,source::MaxDocumentBytes,[&](std::uint64_t){if(!changed){changed=true;int fd=open(large.c_str(),O_WRONLY|O_CLOEXEC);check(fd>=0,"mutate-open");check(pwrite(fd,"!",1,0)==1,"mutate-write");close(fd);}});},"mutation-during-hash");
  big=source::hashFile(large);changed=false;rejected([&]{source::copyFile(large,output,big,[&](std::uint64_t){if(!changed){changed=true;check(!truncate(large.c_str(),0),"mutate-truncate");}});},"mutation-during-copy");check(small(output)=="previous","failed-copy-not-published");
  save(input,"abc");checked=source::hashFile(input);
  for(auto fault:{storage::Fault::Write,storage::Fault::FileSync,storage::Fault::BeforeRename}){storage::injectFault(fault);rejected([&]{source::copyFile(input,output,checked);},"durability-fault");storage::injectFault(storage::Fault::None);check(small(output)=="previous","failed-commit-retains-prior");}
  storage::injectFault(storage::Fault::DirectorySync);rejected([&]{source::copyFile(input,output,checked);},"uncertain-commit-reported");storage::injectFault(storage::Fault::None);check(small(output)=="abc","uncertain-commit-not-misreported-as-rollback");
  for(const auto& entry:fs::directory_iterator(root))check(!entry.path().filename().string().starts_with(".inkbound-pending-"),"no-abandoned-temporaries");
  fs::remove_all(root);std::cout<<"SOURCE_FILE_PASS large_bytes=69206016 bounded_memory=true SHA256=true actual_byte_progress=true mutation_rejected=true durable_faults=true\n";return 0;
 }catch(const std::exception& e){storage::injectFault(storage::Fault::None);std::cerr<<"SOURCE_FILE_FAIL "<<e.what()<<" fixture="<<root<<'\n';return 1;}
}
