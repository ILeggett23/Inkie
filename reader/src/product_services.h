#pragma once
#include "product_store.h"
#include "product_bridge.h"
#include <QJsonArray>
#include <QPointF>
#include <span>
namespace product {
QJsonArray palette(bool highlighter=false);QJsonArray toolSpecs();QJsonObject defaultStyle(const QString&);void validateStyle(const QJsonObject&);
unsigned nearestInk(std::uint32_t rgb,bool highlighter=false); // index into palette(): nearest of the 8 inks by hue-weighted OKLCh (see product_services.cpp)
std::uint32_t inkRgb(const QJsonObject& style); // a stroke's exact color if it has one, else its ink's tuned RGB
QJsonObject exactStyle(QJsonObject style,std::uint32_t rgb); // adds "rgb" and sets "color" to the nearest ink
bool transformPoint(const QJsonArray&,QPointF,QPointF&,bool inverse=false);
QJsonObject freezeContext(const QJsonObject&,const QString&,int first=0,int last=0);
bool compatiblePage(const QJsonObject& document,const QJsonObject& page);
QJsonObject resolveAnchor(const QJsonObject&,const QJsonObject&);
struct AnimationPolicy {bool enabled{true},reducedMotion{},contact{},heavyRendering{},constrainedPower{};unsigned maxParticles{18},maxFrames{3},durationMs{180},maxArea{300000};bool permits(unsigned)const;};
inline constexpr qint64 GeneratedInkByteCap=1536*1024;
qint64 estimatedGeneratedInkBytes(const QJsonArray&,const QJsonObject& anchor);
double fillOpacity(const QJsonObject&);
void renderStroke(std::span<std::uint32_t>,int,int,int,const QJsonObject&,const QJsonArray&);
class Services final {
 std::unique_ptr<library::Lease> documentLease_;Store store_;
public:
 Services(const std::filesystem::path& libraryRoot,const library::Document&);
 explicit Services(const std::filesystem::path& featureDirectoryPath);
 QJsonObject perform(const QJsonObject&);
 QJsonArray performAll(const QJsonArray&); // D2: one transaction for several commands
 void registerEpubLayout(const QString& viewId);
 // Compatibility view contains only this already-owned document's feature record.
 QJsonObject snapshot()const;QJsonArray notes(QString document={})const;Store& store(){return store_;}
};
class StreamBuffer {QString text_;qsizetype displayed_{};bool overflowed_{};public:static constexpr qsizetype MaxCharacters=262144;bool receive(const QString&);QString takeDisplayDelta(bool contact,bool force=false);QString text()const{return text_;}bool overflowed()const{return overflowed_;}};
}
