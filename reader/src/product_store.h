#pragma once
#include <QJsonObject>
#include <QString>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
namespace product {
// Feature-only durable state. No catalogue, source, import, folder or tombstone ownership.
class Store final {
public:
 static constexpr qint64 MaxBytes=64*1024*1024,MaxCacheBytes=16*1024*1024;static constexpr unsigned MaxCacheEntries=8;
 struct CacheStats {unsigned entries{};qint64 encodedBytes{};quint64 hits{},misses{};};static CacheStats cacheStats();
 struct NotesNormalization {bool verified{};QJsonValue revision;};NotesNormalization notesNormalization()const;void markNotesNormalized(QJsonValue revision)noexcept; /* Identity-bound performance hint, never a disk schema flag. */
 Store(std::filesystem::path directory,QJsonObject immutableBinding);
 QJsonObject snapshot()const;void transaction(const std::function<void(QJsonObject&)>&);
 QString error()const;bool recovered()const;
 std::filesystem::path directory()const{return directory_;}
 enum class Fault {None,BeforeCommit,AfterCommit};void injectFault(Fault);
private:
 std::shared_ptr<int> lease_;std::filesystem::path directory_;QJsonObject binding_,state_;
 mutable std::mutex mutex_;QString error_;bool recovered_{},failed_{},notesNormalized_{};Fault fault_{Fault::None};
 void commit(const QJsonObject&);void validate(const QJsonObject&)const;
};
QString newId();QString sha256(const QByteArray&);void require(bool,const char*);
}
