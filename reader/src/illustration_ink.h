// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "native_ink.h"
#include <QByteArray>
namespace tomink {
enum class Detail {QuickSketch,Detailed,Standard,HighlyDetailed};
struct VisualBudget {unsigned strokes{},points{},rasterEdge{};double simplification{};};
VisualBudget visualBudget(Detail,double longestEdge,std::uint64_t workingMemory=64*1024*1024);
struct IllustrationStats {unsigned lines{},fills{},skeleton{},merged{},contrastLines{},paper{},droppedLines{},droppedFills{},droppedHoles{},rasterWidth{},rasterHeight{},linePixels{};double lineCoverage{},lineRetention{},fillRecall{},fillPrecision{},inkRecall{},lineRecall{},coarsen{1};};
struct IllustrationOptions {double x{24},y{24},width{1400},height{1000};Detail detail{Detail::QuickSketch};bool nativeTools{};QString contourTool{"pencil"};QJsonArray components;std::uint64_t workingMemory{64*1024*1024};double coordinateQuantum{1.0/16};bool lineArt{};IllustrationStats* stats{};unsigned reserveStrokes{},reservePoints{};}; /* lineArt: centerline strokes plus bounded fills (ink_trace.cpp); needs nativeTools. reserve*: budget left for ink the caller draws after the image (labels, leaders, captions) */
nativeink::Group illustrate(const QByteArray& pngOrJpeg,const IllustrationOptions& = {},const std::atomic<bool>* stop=nullptr);
nativeink::Group diagram(const QJsonObject& structure,const IllustrationOptions& = {},const std::atomic<bool>* stop=nullptr);
QJsonObject illustrationPrompt(QString prompt,Detail detail=Detail::QuickSketch);
}
