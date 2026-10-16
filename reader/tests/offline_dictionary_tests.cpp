#include "offline_dictionary.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QtEndian>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
static void check(bool ok,const char* label){if(!ok)throw std::runtime_error(label);std::cout<<label<<"=PASS\n";}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;auto provider=inkbound::dictionary::offlineDictionary(argv[1]);check(provider->isAvailable()&&provider->isOffline(),"real_offline_asset_available");check(provider->sourceIdentifier()=="princeton-wordnet-3.0"&&provider->supportedLanguages()==std::vector<std::string>{"en"},"source_and_language");
 for(auto word:{"reader","book","word"}){auto entry=provider->lookup("en",word);check(entry&&entry->headword==word&&entry->datasetVersion=="3.0"&&entry->senses.size()>0&&entry->senses.size()<=128,"real_entry_present");check(entry->pronunciation.empty()&&!entry->sourceAttribution.empty()&&entry->senses[0].partOfSpeech=="noun","source_metadata_no_invented_pronunciation");}
 check(provider->lookup("en","reader")->senses[0].definition=="a person who enjoys reading","reader_source_gloss");check(provider->lookup("en","book")->senses[0].definition.find("a written work or composition")!=std::string::npos,"book_source_gloss");check(provider->lookup("en","word")->senses[0].definition.find("a unit of language")!=std::string::npos,"word_source_gloss");
 check(provider->lookup("en"," Reader ")->headword=="reader","normalized_case_lookup");check(provider->lookup("en","children")->headword=="child","source_exception_entry");
 check(!provider->lookup("en","zzunattestedlexeme987654321")&&!provider->lookup("fr","reader"),"missing_and_unsupported_language");check(!provider->lookup("en",std::string(513,'a'))&&!provider->lookup("en",std::string("\xff\xff",2)),"bounded_invalid_input");
 QTemporaryDir temp;check(temp.isValid(),"test_private_directory");std::filesystem::path root=temp.path().toStdString();auto missing=inkbound::dictionary::offlineDictionary(root/"missing");check(!missing->isAvailable()&&!missing->lookup("en","reader"),"missing_dictionary_unavailable");
 std::filesystem::create_symlink(std::filesystem::absolute(argv[1]),root/"link");check(!inkbound::dictionary::offlineDictionary(root/"link")->isAvailable(),"symlink_dictionary_rejected");
 {std::ofstream bad(root/"bad");bad<<"not a dictionary";}check(!inkbound::dictionary::offlineDictionary(root/"bad")->isAvailable(),"malformed_dictionary_rejected");std::filesystem::resize_file(root/"bad",128U*1024U*1024U+1);check(!inkbound::dictionary::offlineDictionary(root/"bad")->isAvailable(),"oversize_dictionary_rejected");
 check(::mkfifo((root/"fifo").c_str(),0600)==0&&!inkbound::dictionary::offlineDictionary(root/"fifo")->isAvailable(),"nonregular_dictionary_rejected_without_wait");
 QByteArray payload("{\"word\":\"reader\"}");QByteArray corrupt(204+payload.size(),'\0');std::copy_n("IBWN3V1\n",8,corrupt.data());qToLittleEndian<quint32>(1,corrupt.data()+8);qToLittleEndian<quint32>(172,corrupt.data()+12);qToLittleEndian<quint64>(204,corrupt.data()+16);qToLittleEndian<quint64>(corrupt.size(),corrupt.data()+24);std::copy_n("reader",6,corrupt.data()+32);qToLittleEndian<quint64>(204,corrupt.data()+32+128);qToLittleEndian<quint32>(payload.size(),corrupt.data()+32+136);std::copy(payload.begin(),payload.end(),corrupt.begin()+204);{std::ofstream out(root/"corrupt",std::ios::binary);out.write(corrupt.data(),corrupt.size());}
 ::chmod((root/"corrupt").c_str(),0600);auto damaged=inkbound::dictionary::offlineDictionary(root/"corrupt");bool rejected=false;try{(void)damaged->lookup("en","reader");}catch(const std::runtime_error&){rejected=true;}check(damaged->isAvailable()&&rejected,"record_checksum_failure_rejected");
}
