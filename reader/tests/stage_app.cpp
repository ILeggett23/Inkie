#include "ux_support.h"
#include <QCoreApplication>
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=5)return 64;try{auto root=stage(argv[1],argv[2],argv[3],argv[4]);auto dictionary=std::filesystem::path(argv[1])/"fixtures/dictionary-test.json";if(std::filesystem::exists(dictionary))std::filesystem::copy_file(dictionary,root/"fixtures/dictionary-test.json");std::puts(root.c_str());return 0;}catch(const std::exception& e){std::fprintf(stderr,"STAGE_FAIL %s\n",e.what());return 1;}}
