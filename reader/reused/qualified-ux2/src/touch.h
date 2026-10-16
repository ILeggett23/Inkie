#pragma once
#include "chrome.h"
#include <linux/input.h>
namespace reader {
class Touch {
 public: struct Tap {bool valid{};int x{},y{};};
 private: Tap lastTap_{};
 int fd_{-1};int slot_{};std::array<int,16> ids_{};spike::Axis x_{},y_{};
 bool gesture_{},blocked_{},bad_{};int startX_{},startY_{},lastX_{},lastY_{};spike::Ns start_{};
public:
 Touch(){ids_.fill(-1);}~Touch();bool open(bool permitted)noexcept;
 Action feed(const input_event&,bool markerBusy,spike::Ns now)noexcept;
 Action poll(bool markerBusy)noexcept;
 Tap takeTap()noexcept{auto t=lastTap_;lastTap_={};return t;}
 Tap pollTap(bool markerBusy)noexcept{lastTap_={};(void)poll(markerBusy);return takeTap();}
 void block()noexcept{blocked_=true;}
 void quiesce()noexcept{(void)poll(true);bool live=false;for(int id:ids_)live|=id>=0;if(!live)blocked_=false;}
 void testAxes(spike::Axis x,spike::Axis y){x_=x;y_=y;}
};
}
