#include "import_inspector.h"
#include "document_validation.h"
#include "pdf_policy.h"
#include "core/document/MuPdfDocumentEngine.h"
#include <QImage>
#include <QBuffer>
#include <QImageReader>
#include <algorithm>
#include <stdexcept>
namespace library {
Inspection inspectDocument(const std::filesystem::path& path,const QString& format){
 library::validateDocumentContainer(path,format.toStdString());if(format=="pdf")library::validatePdfPolicy(path);inkbound::document::MuPdfDocumentEngine engine;
 if(!engine.openDocument(path))throw std::runtime_error("import-parser-rejected");if(QString::fromStdString(engine.documentMetadata().format).toLower()!=format)throw std::runtime_error("import-format-mismatch");auto total=engine.locationCount();if(total<1||total>1000)throw std::runtime_error("import-page-limit");
 auto bounds=engine.pageBounds(0);if(!bounds||bounds->width<=0||bounds->height<=0)throw std::runtime_error("import-page-bounds");
 QImage image;if(format=="epub"){auto cover=validatedEpubCover(path);if(!cover.isEmpty()){QBuffer input(&cover);input.open(QIODevice::ReadOnly);QImageReader reader(&input);const auto size=reader.size();if(size.width()>0&&size.height()>0&&size.width()<=8192&&size.height()<=8192&&qint64(size.width())*size.height()<=16777216){reader.setScaledSize(size.scaled(384,512,Qt::KeepAspectRatio));image=reader.read();}}}
 if(image.isNull()){auto pixels=engine.renderPage(0,std::min(384.0/bounds->width,512.0/bounds->height),false);if(!pixels)throw std::runtime_error("import-render-rejected");image=QImage(pixels->rgba.data(),pixels->pixelSize.width,pixels->pixelSize.height,pixels->strideBytes,QImage::Format_RGBA8888).copy();}
 image=image.scaled(384,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);QByteArray thumbnail;QBuffer buffer(&thumbnail);buffer.open(QIODevice::WriteOnly);if(!image.save(&buffer,"PNG")||thumbnail.size()>1024*1024)throw std::runtime_error("import-thumbnail");
 return {int(total),QString::fromStdString(engine.layoutIdentity()),thumbnail};
}
}
