/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include "illustration_ink.h"
namespace tomink {
struct VisualTarget {QRectF available{24,24,1400,1000};double margin{12};double readable{12};std::uint64_t workingMemory{64*1024*1024};};
nativeink::Group visualScene(const QJsonObject&,VisualTarget={},const QByteArray& composition={},const std::atomic<bool>* stop=nullptr);
void preflightVisualScene(const QJsonObject&,VisualTarget={},const std::atomic<bool>* stop=nullptr);
QString visualImageSize(const QJsonObject&);
QString visualCompositionPrompt(const QJsonObject&);
}
