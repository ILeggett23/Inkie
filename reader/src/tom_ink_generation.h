// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "illustration_request.h"
#include "native_ink.h"
namespace tomink {nativeink::Group generateInk(const QJsonObject& command,double y,IllustrationProvider*,const std::atomic<bool>& canceled);}
