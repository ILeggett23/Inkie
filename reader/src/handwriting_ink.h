// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "native_ink.h"
namespace tomink {
struct HandwritingOptions {double x{24},y{24},width{1400},height{16000},size{34};unsigned seed{1};QJsonObject style{{"version",1},{"tool","fineliner"},{"width","thick"},{"color","black"}};};
nativeink::Group handwriting(const QString&,const HandwritingOptions& = {},const std::atomic<bool>* stop=nullptr);
}
