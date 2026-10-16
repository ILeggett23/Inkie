#include "underlay.h"
#include "reader.h"
#include <unistd.h>
#include <sys/stat.h>
#include <cstdio>
#include <string_view>
int main(int argc,char**argv){
 umask(0077);bool initial=argc==2&&std::string_view(argv[1])=="--prepare"&&getuid()==0;
 bool page=argc==5&&std::string_view(argv[1])=="--page";if(!initial&&!page)return 78;
 try{char path[4096];auto n=readlink("/proc/self/exe",path,sizeof(path)-1);if(n<=0||n>=4095)return 78;path[n]=0;
  auto root=std::filesystem::path(path).parent_path().parent_path();underlay::Prepared p;
  if(initial)p=reader::initialPrepare(root,underlay::DataPath);
  else{std::string number=argv[3];std::size_t used{};int index=std::stoi(number,&used);if(used!=number.size()||!std::filesystem::path(argv[2]).is_absolute()||!std::filesystem::path(argv[4]).is_absolute())return 78;p=underlay::prepare(argv[4],reader::pagePath(argv[2],index),index,true);}
  std::printf("READER_PREPARED page=%d pages=%d replayed_operations=%llu vendor_calls=0 input_opens=0\n",p.metadata["page"].toInt(),p.metadata["pages"].toInt(),(unsigned long long)p.lastId);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"PREPARATION_FAILED reason=%s\n",e.what());return 78;}
}
