// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Plain sentences for internal status codes. Workers and stores report terse codes ("product-store-already-open",
// "note-layer-hidden-or-locked", "SEMANTIC DATA UNAVAILABLE") that belong in logs, not on screen. Error sites pass
// what they would show through friendly(): a message already written for people (a sentence with spaces, not
// shouted) passes through unchanged; a code becomes the closest plain sentence, and anything unknown becomes a
// calm generic one. Never used for progress or state labels, only for failures and refusals.
#include <QString>
namespace ux {
inline QString friendly(const QString& raw){
 const QString s=raw.trimmed();if(s.isEmpty())return s;
 const bool code=!s.contains(QLatin1Char(' '))||s==s.toUpper();
 if(!code)return s;
 const QString k=s.toLower();auto has=[&](const char* x){return k.contains(QLatin1String(x));};
 if(has("hidden-or-locked"))return QStringLiteral("This layer is hidden or locked — open Layers to write");
 if(has("already-open")||has("lock-open")||has("busy")||has("priority")||has("owner-mode"))return QStringLiteral("Still saving — try again in a moment");
 if(has("capacity")||has("store-full"))return QStringLiteral("There’s no room for more here — undo or delete something first");
 if(has("missing")||has("deleted"))return QStringLiteral("That item is no longer here — your notes are kept");
 if(has("source changed"))return QStringLiteral("The source changed — the note is kept");
 if(has("unavailable")||has("no text"))return QStringLiteral("Text isn’t available on this page");
 if(has("read")||has("open")||has("schema")||has("migration"))return QStringLiteral("This couldn’t be read — your saved work is safe");
 return QStringLiteral("Something went wrong — your work is kept");
}
}
