#pragma once
#include "ib/components.h"
namespace ib::status {
inline constexpr Rect LibraryBar{1080,40,1580,152},ReaderBar{1080,8,1496,104},ReaderClock{1280,8,1496,104},Panel{876,160,1580,920};
inline constexpr int LitLevels=5;
enum Id:int {Open=16000,Close,Wifi,Battery,LightOff,Rotate,LevelBase=16010};
// hot: the tablet is too warm (device::HeatWatch); degrees: the panel sensor's reading for the message, -1000 unknown.
// rotate: auto-rotate (Xochitl's orientation lock, inverted): 1 on, 0 locked, -1 no orientation sensor.
struct Model {int battery{-1},wifi{-1},light{-1},maximum{-1},degrees{-1000},rotate{-1};bool charging{},busy{},clockOnly{},hot{};std::string clock,zone,error;bool operator==(const Model&)const=default;};
int nativeLevel(int step,int maximum)noexcept;
int nearestStep(int level,int maximum)noexcept;
void bar(Canvas&,Targets&,const Model&,Rect);
void panel(Canvas&,Targets&,const Model&);
}
