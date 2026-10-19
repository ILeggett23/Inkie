// SPDX-License-Identifier: AGPL-3.0-only
// ux::friendly(): internal status codes become plain sentences on screen; messages already written for people pass
// through. Codes are the ones workers, stores and semantic lookups actually report (grep for them before adding).
#include "user_messages.h"
#include <QStringList>
#include <cstdio>
#include <stdexcept>
namespace {unsigned checks=0;void check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
// A sentence a person can read: no hyphenated code word, not shouted, starts with a capital.
bool sentence(const QString& s){if(s.isEmpty()||!s.at(0).isUpper()||s==s.toUpper())return false;for(const auto& w:s.split(QLatin1Char(' ')))if(w.count(QLatin1Char('-'))>=2)return false;return s.contains(QLatin1Char(' '));}}
int main(){try{
 const char* codes[]{"note-layer-hidden-or-locked","product-store-already-open","product-store-lock-open","worker-busy","owner-mode",
  "product-store-capacity","note-missing","layer-deleted","SEMANTIC DATA UNAVAILABLE","NO TEXT LAYER","SOURCE CHANGED",
  "product-store-read","schema-version-unsupported","migration-failed","tom_store_open","unrecognised-thing","E_UNKNOWN"};
 for(const char* c:codes){const auto out=ux::friendly(QString::fromLatin1(c));check(sentence(out),"every code becomes a sentence");const QString code=QString::fromLatin1(c);if(code.contains(QLatin1Char('-'))||code.contains(QLatin1Char('_')))check(!out.contains(code,Qt::CaseInsensitive),"a code word never reaches the screen");}
 check(ux::friendly("note-layer-hidden-or-locked").contains("Layers"),"a locked layer names where to fix it");
 check(ux::friendly("product-store-already-open").contains("try again"),"a busy store asks for a retry");
 const QString written="The bookmark could not be saved";check(ux::friendly(written)==written,"written messages pass through unchanged");
 check(ux::friendly("  ").isEmpty()&&ux::friendly({}).isEmpty(),"nothing to say stays empty");
 std::printf("USER_MESSAGES_TESTS_PASS checks=%u\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"USER_MESSAGES_TESTS_FAIL %s checks=%u\n",e.what(),checks);return 1;}}
