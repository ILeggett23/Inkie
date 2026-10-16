/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <QJsonObject>
#include <QString>
namespace companion {
inline bool scenePurpose(const QString&p){return p=="auto-scene-answer"||p=="read-auto-scene-answer";}
inline unsigned automaticVersion(const QString&p){return scenePurpose(p)?2:1;}
inline bool readPurpose(const QString&p){return p=="read-answer"||p=="read-auto-answer"||p=="read-auto-scene-answer";}
inline bool automaticPurpose(const QString& p){return p=="auto-answer"||p=="read-auto-answer"||scenePurpose(p);}
inline QString answeredQuestion(const QJsonObject& turn){return readPurpose(turn.value("purpose").toString())?turn.value("read_question").toString():turn.value("question").toString();} /* a read request answers the handwriting Tom read (its persisted question event); the typed field is empty by contract */
struct AutomaticReply {QString response;QJsonObject visual;bool complete{};QString mode;};
AutomaticReply automaticReply(const QString&,bool complete,unsigned schemaVersion=1);
QString automaticDisplay(const QString&,bool complete,unsigned schemaVersion=1);
}
