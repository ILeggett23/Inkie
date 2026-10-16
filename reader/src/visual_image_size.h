/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <QString>
#include <QSize>
#include <QRegularExpression>
#include <stdexcept>
namespace companion {inline QSize checkedVisualImageSize(const QString&size){auto m=QRegularExpression("^([0-9]{3,4})x([0-9]{3,4})$").match(size);if(!m.hasMatch())throw std::runtime_error("visual-image-size");int w=m.captured(1).toInt(),h=m.captured(2).toInt();if(w<512||h<512||w>1536||h>1536||w%16||h%16||w*h<655360||w*h>1536*1024||w>3*h||h>3*w||size!=QString::number(w)+"x"+QString::number(h))throw std::runtime_error("visual-image-size");return {w,h};}}
