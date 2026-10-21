#pragma once
#include "core.h"
#include <QJsonObject>
#include <QStringList>
namespace ux {
class CompanionView {
public:
 enum class Action {None,Changed,Close,StartPair,PollPair,Approve,Reject,Sync,RetryTransfer,Unpair};
 struct Hit {Action action{Action::None};QString expectedPairId,expectedAccount;};
 void open(const QJsonObject& status);void update(const QJsonObject& status);
 bool active()const{return active_;}
 void paint(spike::Surface);Hit hit(int x,int y);
 // The shell could not start the request (a helper is still running): the tap is undone, so the button is shown
 // again and an approval is not reported as consumed.
 void refused();
 // Exact text used by paint; no raw status dump or credential fields.
 QStringList displayedText()const;
private:
 QJsonObject status_;bool active_{},painted_{},pending_{};
 QString paintedPair_,paintedAccount_,consumedPair_,priorConsumed_;bool confirmUnpair_{};
 Action paintedPrimary_{Action::None},paintedSecondary_{Action::None};
 bool accountSupported()const;bool livePair()const;bool backoff()const;bool repair()const;
 Action primary()const;Action secondary()const;
};
}
