#include "underlay.h"
#include <unistd.h>
#include <sys/stat.h>
#include <cstdio>
#include <string_view>
int main(int argc,char**argv){
 umask(0077);if(argc!=2||std::string_view(argv[1])!="--prepare"||getuid()!=0)return 78;
 try{char path[4096];auto n=readlink("/proc/self/exe",path,sizeof(path)-1);if(n<=0||n>=4095)return 78;path[n]=0;
  auto p=underlay::prepare(std::filesystem::path(path).parent_path().parent_path(),underlay::DataPath);
  std::printf("DOCUMENT_PREPARED page=0 monochrome=threshold192 replayed_operations=%llu vendor_calls=0 input_opens=0\n",(unsigned long long)p.lastId);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"PREPARATION_FAILED reason=%s\n",e.what());return 78;}
}
