#include "pdf_policy.h"
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}
namespace library {
namespace {
constexpr std::size_t MaxBytes=64U*1024U*1024U;
constexpr unsigned MaxNodes=1000000;
const char* forbidden(const char* name){
 if(!std::strcmp(name,"JavaScript")||!std::strcmp(name,"JS"))return "pdf-policy-javascript";
 if(!std::strcmp(name,"OpenAction")||!std::strcmp(name,"AA"))return "pdf-policy-automatic-action";
 if(!std::strcmp(name,"Launch")||!std::strcmp(name,"SubmitForm")||!std::strcmp(name,"ImportData")||!std::strcmp(name,"GoToR")||!std::strcmp(name,"GoToE"))return "pdf-policy-external-action";
 if(!std::strcmp(name,"XFA"))return "pdf-policy-xfa";
 if(!std::strcmp(name,"EmbeddedFiles")||!std::strcmp(name,"EmbeddedFile")||!std::strcmp(name,"EF")||!std::strcmp(name,"FileAttachment"))return "pdf-policy-embedded-file";
 if(!std::strncmp(name,"RichMedia",9)||!std::strcmp(name,"Rendition")||!std::strcmp(name,"Movie")||!std::strcmp(name,"Sound")||!std::strcmp(name,"3D"))return "pdf-policy-active-media";
 return nullptr;
}
// Only POD locals here: MuPDF errors longjmp to validatePdfPolicy's fz_try.
// Do not follow references; every live xref object is scanned independently.
const char* scan(fz_context* ctx,pdf_obj* object,unsigned depth,unsigned* nodes){
 if(depth>64||++*nodes>MaxNodes)return "pdf-policy-structure-limit";
 if(pdf_is_indirect(ctx,object))return nullptr;
 if(pdf_is_name(ctx,object))return forbidden(pdf_to_name(ctx,object));
 if(pdf_is_dict(ctx,object)){
  const int count=pdf_dict_len(ctx,object);
  for(int i=0;i<count;++i){
   const char* reason=scan(ctx,pdf_dict_get_key(ctx,object,i),depth+1,nodes);if(reason)return reason;
   reason=scan(ctx,pdf_dict_get_val(ctx,object,i),depth+1,nodes);if(reason)return reason;
  }
 }else if(pdf_is_array(ctx,object)){
  const int count=pdf_array_len(ctx,object);
  for(int i=0;i<count;++i){const char* reason=scan(ctx,pdf_array_get(ctx,object,i),depth+1,nodes);if(reason)return reason;}
 }
 return nullptr;
}
struct Fd{int value{-1};~Fd(){if(value>=0)::close(value);}};
}
void validatePdfPolicy(const std::filesystem::path& path){
 Fd file{::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK)};
 if(file.value<0)throw std::runtime_error("pdf-policy-source");
 struct stat info{};
 if(::fstat(file.value,&info)||!S_ISREG(info.st_mode)||info.st_size<=0||static_cast<unsigned long long>(info.st_size)>MaxBytes)throw std::runtime_error("pdf-policy-size");
 std::vector<unsigned char> bytes(static_cast<std::size_t>(info.st_size));std::size_t offset=0;
 while(offset<bytes.size()){
  const auto read=::read(file.value,bytes.data()+offset,bytes.size()-offset);
  if(read<0&&errno==EINTR)continue;
  if(read<=0)throw std::runtime_error("pdf-policy-read");offset+=static_cast<std::size_t>(read);
 }
 fz_context* ctx=fz_new_context(nullptr,nullptr,32U*1024U*1024U);
 if(!ctx)throw std::runtime_error("pdf-policy-context");
 fz_set_error_callback(ctx,[](void*,const char*){},nullptr);
 fz_set_warning_callback(ctx,[](void*,const char*){},nullptr);
 fz_stream* stream=nullptr;pdf_document* doc=nullptr;pdf_obj* object=nullptr;
 const char* failure=nullptr;unsigned nodes=0;int failed=0;
 fz_var(stream);fz_var(doc);fz_var(object);fz_var(failure);fz_var(nodes);fz_var(failed);
 fz_try(ctx){
  stream=fz_open_memory(ctx,bytes.data(),bytes.size());
  doc=pdf_open_document_with_stream(ctx,stream);
  if(doc->crypt!=nullptr||pdf_needs_password(ctx,doc))failure="pdf-policy-encrypted";
  if(!failure&&(pdf_count_objects(ctx,doc)<1||pdf_count_objects(ctx,doc)>100000))failure="pdf-policy-xref-limit";
  if(!failure)failure=scan(ctx,pdf_trailer(ctx,doc),0,&nodes);
  for(int i=1;!failure&&i<pdf_count_objects(ctx,doc);++i){
   if(pdf_count_objects(ctx,doc)>100000){failure="pdf-policy-xref-limit";break;}
   if(pdf_get_xref_entry(ctx,doc,i)->type=='f')continue;
   object=pdf_load_object(ctx,doc,i);
   failure=scan(ctx,object,0,&nodes);
   pdf_drop_obj(ctx,object);object=nullptr;
  }
  if(!failure&&pdf_count_objects(ctx,doc)>100000)failure="pdf-policy-xref-limit";
 }
 fz_always(ctx){pdf_drop_obj(ctx,object);pdf_drop_document(ctx,doc);fz_drop_stream(ctx,stream);}
 fz_catch(ctx){failed=1;}
 fz_drop_context(ctx);
 if(failed)throw std::runtime_error("pdf-policy-parse");
 if(failure)throw std::runtime_error(failure);
}
}
