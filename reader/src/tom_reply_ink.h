// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "stroke_playback.h"
#include "work_tracking.h"
#include "illustration_request.h"
#include <filesystem>
namespace tom {
class ReplyInk {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 inline static constexpr spike::Rect Area{70,680,1550,1320};
 ReplyInk(std::filesystem::path app,std::filesystem::path library,reader::lifecycle::Tracking tracking={},std::shared_ptr<tomink::IllustrationProvider> provider={});~ReplyInk();
 void reset();bool submit(QString request,QJsonObject generation,bool complete);bool tick(bool penBusy=false,std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now());
 bool scrollBy(int delta,bool user=true);void finishInstantly();bool playing()const noexcept;bool quiet()const noexcept;bool busy()const noexcept;bool ready()const noexcept;bool complete()const noexcept;bool colored()const noexcept;QString request()const;QString error()const;QJsonArray strokes()const;QString kind()const;int scroll()const noexcept;std::uint64_t revision()const noexcept;
 spike::Rect damage()const noexcept;void clearDamage()noexcept;void composite(spike::Surface,std::span<const std::uint32_t> paper,spike::Rect)const noexcept;bool drained()const noexcept;bool finish(unsigned milliseconds=100)noexcept;
};
}
