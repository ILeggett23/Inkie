#include <QImage>
#include <QtGlobal>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
int main(){
 Dl_info gui{},core{},ctor{};
 void* c=dlsym(RTLD_NEXT,"_ZN6QImageC1EPhiixNS_6FormatEPFvPvES2_");
 if(!c||!dladdr(&QImage::staticMetaObject,&gui)||!dladdr(reinterpret_cast<void*>(&qVersion),&core)||!dladdr(c,&ctor))return 1;
 if(!strstr(gui.dli_fname,"libQt6Gui.so")||!strstr(core.dli_fname,"libQt6Core.so")||!strstr(ctor.dli_fname,"libQt6Gui.so"))return 2;
 std::puts("QT_ORIGIN_TEST_PASS imported_metaobject_and_functions_in_expected_dsos vendor_calls=0");return 0;
}
