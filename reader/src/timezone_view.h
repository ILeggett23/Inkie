// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "text_input.h"
#include <QStringList>
#include <optional>
namespace ux {
class TimezoneView {
 QStringList all_,visible_;QString selected_,filter_;unsigned offset_{};bool active_{};TextInput search_;ib::Targets targets_;std::optional<QString> choice_;void rebuild();
public:
 enum Id:int {Close=17400,Search,Previous,Next,ZoneBase=17410};static constexpr spike::Rect Panel{300,220,1560,2060};static constexpr unsigned PageSize=10;
 void open(QString);bool active()const noexcept{return active_;}void paint(spike::Surface);bool finger(int,int);std::optional<QString> takeChoice(){auto c=choice_;choice_.reset();return c;}const ib::Targets& targets()const noexcept{return targets_;}const ib::Targets& keyboardTargets()const noexcept{return search_.targets();}
};
}
