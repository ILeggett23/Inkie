#include "pdf_policy.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
static void require(bool v,const char* label){if(!v)throw std::runtime_error(label);std::cout<<label<<"=PASS\n";}
static void pdf(const std::filesystem::path& path,std::string catalogExtra={},std::string pageExtra={},std::vector<std::string> extra={}){
 std::vector<std::string> objects{"<< /Type /Catalog /Pages 2 0 R "+catalogExtra+" >>","<< /Type /Pages /Kids [3 0 R] /Count 1 >>","<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] "+pageExtra+" >>"};
 objects.insert(objects.end(),extra.begin(),extra.end());std::string bytes="%PDF-1.7\n";std::vector<std::size_t> offsets{0};
 for(std::size_t i=0;i<objects.size();++i){offsets.push_back(bytes.size());bytes+=std::to_string(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n";}
 auto start=bytes.size();bytes+="xref\n0 "+std::to_string(offsets.size())+"\n0000000000 65535 f \n";
 for(std::size_t i=1;i<offsets.size();++i){char line[32];std::snprintf(line,sizeof line,"%010llu 00000 n \n",static_cast<unsigned long long>(offsets[i]));bytes+=line;}
 bytes+="trailer\n<< /Size "+std::to_string(offsets.size())+" /Root 1 0 R >>\nstartxref\n"+std::to_string(start)+"\n%%EOF\n";
 std::ofstream file(path,std::ios::binary);file<<bytes;if(!file)throw std::runtime_error("fixture-write");
}
static void rejects(const std::filesystem::path& path,const char* reason,const char* label){try{library::validatePdfPolicy(path);}catch(const std::runtime_error&e){require(std::string(e.what())==reason,label);return;}throw std::runtime_error(label);}
int main(int argc,char**argv){
 auto dir=std::filesystem::temp_directory_path()/("pdf-policy-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(dir);
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}}cleanup{dir};auto path=dir/"fixture.pdf";
 pdf(path);library::validatePdfPolicy(path);require(true,"benign_page_allowed");
 pdf(path,"","/Annots [4 0 R]",{"<< /Type /Annot /Subtype /Link /Rect [0 0 40 40] /A << /S /URI /URI (https://example.invalid) >> >>"});library::validatePdfPolicy(path);require(true,"inert_uri_allowed");
 pdf(path,"/OpenAction [3 0 R /Fit]");rejects(path,"pdf-policy-automatic-action","open_action_rejected");
 pdf(path,"","/AA << /O << /S /GoTo /D [3 0 R /Fit] >> >>");rejects(path,"pdf-policy-automatic-action","additional_action_rejected");
 pdf(path,"","",{"<< /S /JavaScript /JS (app.alert\\(1\\)) >>"});rejects(path,"pdf-policy-javascript","unreferenced_javascript_rejected");
 pdf(path,"/Names << /Java#53cript << /Names [(x) 4 0 R] >> >>","",{"<< /S /Java#53cript /JS (x) >>"});rejects(path,"pdf-policy-javascript","escaped_javascript_name_rejected");
 pdf(path,"","/Annots [4 0 R]",{"<< /A << /S /Launch /F (ignored) >> >>"});rejects(path,"pdf-policy-external-action","launch_rejected");
 pdf(path,"","",{"<< /S /SubmitForm /F (ignored) >>"});rejects(path,"pdf-policy-external-action","submit_form_rejected");
 pdf(path,"","",{"<< /S /ImportData /F (ignored) >>"});rejects(path,"pdf-policy-external-action","import_data_rejected");
 pdf(path,"/AcroForm << /XFA (payload) >>");rejects(path,"pdf-policy-xfa","xfa_rejected");
 pdf(path,"/Names << /EmbeddedFiles << /Names [] >> >>");rejects(path,"pdf-policy-embedded-file","embedded_files_rejected");
 pdf(path,"","",{"<< /Type /EmbeddedFile /Length 0 >>\nstream\n\nendstream"});rejects(path,"pdf-policy-embedded-file","embedded_stream_rejected");
 pdf(path,"","/Annots [4 0 R]",{"<< /Subtype /RichMedia >>"});rejects(path,"pdf-policy-active-media","rich_media_rejected");
 pdf(path,"","",{"<< /S 5 0 R >>","/Launch"});rejects(path,"pdf-policy-external-action","indirect_action_name_rejected");
 pdf(path,"","",{"<< /Reference 4 0 R >>"});library::validatePdfPolicy(path);require(true,"indirect_cycle_bounded");
 pdf(path,"/MetadataText (JavaScript Launch OpenAction)");library::validatePdfPolicy(path);require(true,"ordinary_strings_not_keywords");
 pdf(path,"","",{std::string(70,'[')+"0"+std::string(70,']')});rejects(path,"pdf-policy-structure-limit","deep_arrays_rejected");
 pdf(path,"","",std::vector<std::string>(99997,"null"));rejects(path,"pdf-policy-xref-limit","xref_limit_rejected");
 {std::ofstream file(path);file<<"not a pdf";}rejects(path,"pdf-policy-parse","malformed_rejected");
 std::filesystem::resize_file(path,64U*1024U*1024U+1);rejects(path,"pdf-policy-size","oversized_source_rejected");std::filesystem::remove(path);
 std::filesystem::create_symlink(dir/"missing",path);rejects(path,"pdf-policy-source","symlink_rejected");
 if(argc==2){library::validatePdfPolicy(argv[1]);require(true,"original_fixture_allowed");}else throw std::runtime_error("original-fixture-argument-required");
}
