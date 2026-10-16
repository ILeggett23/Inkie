#pragma once
#include "work_tracking.h"
#include <filesystem>
#include <QJsonObject>
#include <thread>
#include <atomic>
namespace device::frontlight {
struct View {bool available{},busy{},changing{};int requested{-1},actual{-1},maximum{-1},power{-1};QString error;};
class Control {
 reader::lifecycle::Tracking tracking_;std::thread worker_;std::atomic<bool> stop_{false},done_{false};bool stopping_{},writing_{};View view_,result_;
public:
 explicit Control(reader::lifecycle::Tracking tracking={}):tracking_(tracking){}~Control();
 bool request(const std::filesystem::path& app,int level=-1);bool tick();void beginQuiesce()noexcept{stopping_=true;stop_=true;}bool drained()const noexcept{return !worker_.joinable();}View view()const{auto v=view_;v.busy=worker_.joinable();v.changing=v.busy&&writing_;return v;} /* changing: a level set is running; a get keeps the last values */
};
}
