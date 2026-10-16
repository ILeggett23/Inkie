// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Page overview thumbnails (mockup 17). Pages are parsed and rendered only in the isolated helper
// (reader-experience1-prepare --thumbnails) and come back over a pipe as small gray images: nothing is written to
// the document's storage, so no store validator, entry limit or recovery ledger sees them. A thumbnail shows the
// page content as laid out in the active view; handwriting is not drawn into it (the live ink journal stays owned by
// the reader). The shell asks only while Page overview is open and never while a page is being prepared or written.
#include <QByteArray>
#include <QString>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <thread>
#include <vector>
namespace thumbs {
inline constexpr int MaxWidth=216,MaxHeight=288,MaxBatch=12;
struct Image {int page{-1},width{},height{};std::vector<std::uint8_t> gray;};
// Wire form: "IBTHUMB1", u32 count, then per image u32 page, u16 width, u16 height and width*height gray bytes
// (little endian). decode() accepts exactly the pages [first,first+count) in order, each within the size bounds.
inline constexpr std::size_t MaxWireBytes=16+std::size_t(MaxBatch)*(8+std::size_t(MaxWidth)*MaxHeight);
QByteArray encode(const std::vector<Image>&);
std::optional<std::vector<Image>> decode(const QByteArray&,int first,int count);
// Box-filtered reduction into at most MaxWidth x MaxHeight, keeping the page's aspect. rgb32 is 0xAARRGGBB words,
// rgba is R,G,B,A bytes (MuPDF pixmaps).
Image fromRgb32(int page,const std::uint32_t* pixels,int width,int height,int stridePixels);
Image fromRgba(int page,const std::uint8_t* pixels,int width,int height,int strideBytes);
// One document view: the active PDF view set or EPUB layout (empty = import baseline) and its page count.
struct Key {std::filesystem::path app,data;QString view;int pages{};bool operator==(const Key&)const=default;};
// Shell side. One helper at a time on a background thread; results are collected on the shell thread.
class Service {
public:
 Service()=default;~Service();Service(const Service&)=delete;Service& operator=(const Service&)=delete;
 // Pages [first,first+count) of this view, clamped to the page count. False when nothing new is needed (all cached,
 // already running, earlier refused) or a different batch is still running (ask again after collect()).
 bool request(const Key&,int first,int count);
 // Shell thread: joins a finished helper and caches its images. True when new images arrived for the current key.
 bool collect();
 const Image* find(int page)const noexcept{auto i=cache_.find(page);return i==cache_.end()?nullptr:&i->second;}
 void cancel()noexcept;          // stops a running helper (SIGTERM, then SIGKILL); collect() then reaps it
 void reset()noexcept;           // cancel and forget everything (document closed or its view changed)
 bool busy()const noexcept{return worker_.joinable();}
 bool drained()const noexcept{return !worker_.joinable();}
 unsigned refusals()const noexcept{return refusals_;}
private:
 Key key_;std::map<int,Image> cache_;std::set<int> refused_;unsigned refusals_{};
 std::thread worker_;std::atomic<bool> stop_{false},done_{false};int first_{},count_{};Key running_;
 std::optional<std::vector<Image>> result_;
 static std::optional<std::vector<Image>> run(const Key&,int first,int count,const std::atomic<bool>& stop);
};
inline constexpr std::size_t CacheLimit=96;
}
