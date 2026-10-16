#pragma once
#include <QJsonObject>
#include <QVector>
#include <QString>
#include <filesystem>
#include <functional>
#include <stdexcept>
namespace library {
struct RejectedDocument:std::runtime_error{using std::runtime_error::runtime_error;};
constexpr std::size_t MaxSourceBytes=64*1024*1024;
struct Document {
 QString id,sha256,title,format,folder,layout;int pages{},position{};qint64 opened{};bool trashed{};int readingPages{};
};
struct Inspection {int pages{};QString layout;QByteArray thumbnail;};
using Inspector=std::function<Inspection(const std::filesystem::path&,const QString&)>;
bool validId(const QString&);bool validHash(const QString&);
QVector<Document> documents(const std::filesystem::path& root,bool trash=false,QString query={},QString folder={},bool recent=false);
std::filesystem::path documentRoot(const std::filesystem::path&,const QString&);
// Descriptor is immutable. Existing fixture callers without one retain legacy
// layout identity solely for the preserved qualification harness.
QJsonObject descriptor(const std::filesystem::path& documentData);
std::filesystem::path sourcePath(const std::filesystem::path& documentData,const QJsonObject&);
class Lease {int fd_{-1};public: explicit Lease(const std::filesystem::path&,bool exclusive=false);~Lease();Lease(const Lease&)=delete;};
class Store {
 std::filesystem::path root_;int lock_{-1};QVector<Document> docs_,durable_;bool failed_{};void save();
public:
 explicit Store(const std::filesystem::path&);~Store();Store(const Store&)=delete;
 Document importDocument(const std::filesystem::path&,QString title,const Inspector&,bool intentionalNew=false,QString requestedId={},QString expectedSha256={});
 void rename(const QString&,QString);void organize(const QString&,QString);
 void trash(const QString&,bool);void permanentlyDelete(const QString&,bool confirmed);
 void position(const QString&,int);void recover();
 const QVector<Document>& all()const{return docs_;}
};
}
