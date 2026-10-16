#pragma once
#include <QByteArray>
#include <cstdint>
#include <filesystem>
#include <functional>
namespace reader::source {
inline constexpr std::uint64_t MaxDocumentBytes=(1ULL<<53)-1; // exact JSON integer
struct Identity {std::uint64_t device{},inode{},size{};std::int64_t modifiedSeconds{},modifiedNanos{},changedSeconds{},changedNanos{};bool operator==(const Identity&)const=default;};
struct HashedFile {QByteArray digest;Identity identity;};
using Progress=std::function<void(std::uint64_t)>; // actual bytes processed
HashedFile hashFile(const std::filesystem::path&,std::uint64_t maximum=MaxDocumentBytes,const Progress& progress={});
// The checked source identity must still match before AND after copying.
// Publication uses the existing durable private-name/fsync/rename primitive.
void copyFile(const std::filesystem::path& source,const std::filesystem::path& destination,const HashedFile&,const Progress& progress={});
}
