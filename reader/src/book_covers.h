#pragma once
#include "work_tracking.h"
#include <QHash>
#include <QSet>
#include <QString>
#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>
namespace covers {
inline constexpr int Width=384,Height=512,CacheLimit=12;
struct Image {std::vector<std::uint32_t> pixels;int width{},height{};};
struct Key {std::filesystem::path app,data;QString document,source;bool operator==(const Key&)const=default;};
class Service {
 struct Entry {std::shared_ptr<const Image> image;std::uint64_t used{};};
 reader::lifecycle::Tracking tracking_;std::thread worker_;std::atomic<bool> stop_{false},done_{false};bool stopping_{};Key running_;std::shared_ptr<const Image> result_;QHash<QString,Entry> cache_;QSet<QString> refused_;std::uint64_t age_{};unsigned failures_{};
 static QString identity(const QString& id,const QString& source){return id+":"+source;}
public:
 explicit Service(reader::lifecycle::Tracking tracking={}):tracking_(tracking){}~Service();
 std::shared_ptr<const Image> find(const QString&,const QString&);bool request(const Key&);QString collect();void cancel()noexcept{stop_=true;}void beginQuiesce()noexcept{stopping_=true;cancel();}bool drained()const noexcept{return !worker_.joinable();}bool busy()const noexcept{return worker_.joinable();}unsigned failures()const noexcept{return failures_;}int cached()const noexcept{return cache_.size();}
};
}
