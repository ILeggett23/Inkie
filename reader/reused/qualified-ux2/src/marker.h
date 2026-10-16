// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "core.h"
#include <linux/input.h>
namespace spike {
struct MarkerMetadata {
 std::array<char,128> name{};unsigned bus{};bool pen{},rubber{},touch{},absX{},absY{},pressure{};
 Axis x{},y{},p{};int resolutionX{},resolutionY{};
 bool valid()const noexcept;
};
struct PressureEvidence {std::uint64_t frames{},pressureFrames{},rubberFrames{},drops{};int min{},max{};};
enum class FrameResult {None,Ready,Fault};
class FrameAssembler {
 Axis pressureAxis_;bool pressureAvailable_;int x_{},y_{},pressure_{};
 bool haveX_{},haveY_{},havePressure_{},pen_{},rubber_{},touch_{},contact_{},contactRubber_{},changed_{},fault_{};
 PressureEvidence evidence_{};
 FrameResult fail()noexcept;
public:
 explicit FrameAssembler(Axis pressure={0,4095},bool available=true):pressureAxis_(pressure),pressureAvailable_(available){}
 FrameResult feed(unsigned type,unsigned code,int value,Ns,Sample&)noexcept;
 bool cancelled()const noexcept{return fault_&&!contact_&&!touch_&&pressure_==0;}
 const PressureEvidence& evidence()const noexcept{return evidence_;}
 bool contactActive()const noexcept{return contact_;}
};
// Batch validator refuses an incomplete input_event instead of guessing alignment.
bool validEventBytes(std::size_t bytes)noexcept;
struct Marker {
 int fd{-1};MarkerMetadata metadata{};bool grabbed{};
 Marker()=default;Marker(const Marker&)=delete;Marker&operator=(const Marker&)=delete;
 bool openExclusive(bool exclusive)noexcept;
 void close()noexcept;
 ~Marker(){close();}
};
}
