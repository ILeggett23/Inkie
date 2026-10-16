// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "annotation_geometry.h"
#include "styled_ink.h"
namespace notes {
struct Indicator {QString id;spike::Rect bounds;unsigned color{};};
std::vector<Indicator> indicators(const QJsonObject& document,const QJsonObject& page,styled::Matrix matrix);
void paintIndicators(spike::Surface,std::span<const Indicator>);
}
