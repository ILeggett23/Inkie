#pragma once
#include "ib/components.h"
namespace ib::status {
inline constexpr Rect LibraryBar{1080,40,1580,152},ReaderBar{1080,8,1496,104},ReaderClock{1280,8,1496,104},Panel{876,160,1580,816};
inline constexpr int LitLevels=5;
enum Id:int {Open=16000,Close,Wifi,Battery,LightOff,LevelBase=16010};
// hot: the tablet is too warm (device::HeatWatch); degrees: the panel sensor's reading for the message, -1000 unknown.
struct Model {int battery{-1},wifi{-1},light{-1},maximum{-1},degrees{-1000};bool charging{},busy{},clockOnly{},hot{};std::string clock,zone,error;bool operator==(const Model&)const=default;};
int nativeLevel(int step,int maximum)noexcept;
int nearestStep(int level,int maximum)noexcept;
void bar(Canvas&,Targets&,const Model&,Rect);
void panel(Canvas&,Targets&,const Model&);
}
