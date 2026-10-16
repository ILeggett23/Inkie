// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// EPUB text and layout preferences (native "Text and layout"): font, line spacing, margins and justification,
// expressed as one MuPDF user stylesheet that becomes part of the layout identity. The book's own choices are
// the default and produce an EMPTY stylesheet, so every existing layout keeps its identity byte for byte.
// Only stylesheets generated here are accepted anywhere (layout schema checks call valid()).
#include <QString>
#include <QStringList>
#include <array>
#include <optional>
namespace epub {
enum class FontFamily:unsigned {Book,Serif,Sans};
enum class Step:unsigned {Small,Medium,Large};          // Medium is the book's own spacing or margins
enum class Alignment:unsigned {Book,Left,Justified};
struct Typography {
 FontFamily family{FontFamily::Book};Step spacing{Step::Medium},margins{Step::Medium};Alignment alignment{Alignment::Book};
 bool operator==(const Typography&)const=default;
};
inline QString css(const Typography& t){
 QStringList rules;
 if(t.family==FontFamily::Serif)rules<<"body,p,div,span,li,blockquote,h1,h2,h3,h4,h5,h6{font-family:serif !important}";
 if(t.family==FontFamily::Sans)rules<<"body,p,div,span,li,blockquote,h1,h2,h3,h4,h5,h6{font-family:sans-serif !important}";
 if(t.spacing==Step::Small)rules<<"body,p,div,li,blockquote{line-height:1.2 !important}";
 if(t.spacing==Step::Large)rules<<"body,p,div,li,blockquote{line-height:1.75 !important}";
 if(t.margins==Step::Small)rules<<"body{margin-left:0 !important;margin-right:0 !important}";
 if(t.margins==Step::Large)rules<<"body{margin-left:3em !important;margin-right:3em !important}";
 if(t.alignment==Alignment::Left)rules<<"p,div,li,blockquote{text-align:left !important}";
 if(t.alignment==Alignment::Justified)rules<<"p,div,li,blockquote{text-align:justify !important}";
 return rules.join('\n');
}
// The typography a stylesheet was generated from; nothing for any other text (81 combinations).
inline std::optional<Typography> parse(const QString& text){
 for(unsigned f=0;f<3;++f)for(unsigned s=0;s<3;++s)for(unsigned m=0;m<3;++m)for(unsigned a=0;a<3;++a){
  Typography t{FontFamily(f),Step(s),Step(m),Alignment(a)};if(css(t)==text)return t;}
 return std::nullopt;
}
inline bool valid(const QString& text){return parse(text).has_value();}
// Compact helper-argument form: "family,spacing,margins,alignment" as digits, e.g. "1,1,2,0".
inline QString token(const Typography& t){return QString("%1,%2,%3,%4").arg(unsigned(t.family)).arg(unsigned(t.spacing)).arg(unsigned(t.margins)).arg(unsigned(t.alignment));}
inline std::optional<Typography> fromToken(const QString& value){
 const auto parts=value.split(',');if(parts.size()!=4)return std::nullopt;std::array<unsigned,4> v{};
 for(int i=0;i<4;++i){bool ok=false;v[std::size_t(i)]=parts[i].toUInt(&ok);if(!ok||v[std::size_t(i)]>2||parts[i]!=QString::number(v[std::size_t(i)]))return std::nullopt;}
 return Typography{FontFamily(v[0]),Step(v[1]),Step(v[2]),Alignment(v[3])};
}
}
