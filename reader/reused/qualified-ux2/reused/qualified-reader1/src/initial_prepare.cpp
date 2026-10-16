#include "reader.h"
#include "core/document/MuPdfDocumentEngine.h"
namespace reader {
underlay::Prepared initialPrepare(const std::filesystem::path& app,const std::filesystem::path& data){
 auto bytes=underlay::readBounded(app/"fixtures/inkbound-device-test.pdf",1024*1024);if(underlay::digest(bytes).toHex()!=underlay::FixtureHash)throw std::runtime_error("document-identity");
 inkbound::document::MuPdfDocumentEngine doc;if(!doc.openDocument(app/"fixtures/inkbound-device-test.pdf"))throw std::runtime_error(doc.lastError());int total=int(doc.locationCount());doc.closeDocument();
 underlay::privateDirectory(data);auto resume=readResume(data,total);auto p=underlay::prepare(app,pagePath(data,resume.page),resume.page,false);underlay::savePrepared(data/"current.prepared",p);return p;
}
}
