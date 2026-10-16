/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include "illustration_ink.h"
#include <QImage>
namespace tomink {inline constexpr double InkWorkingBytesPerPixel=64; /* measured VM peak for the two-attempt lineArt path, about 63 B per raster pixel (converter r1 report) */
nativeink::Group traceInk(const QImage& argb,const IllustrationOptions&,const VisualBudget&,const std::atomic<bool>* stop);}
