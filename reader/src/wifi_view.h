// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "core.h"
#include "wifi_control.h"
#include <QStringList>
#include <vector>
namespace ux {
// Library Wi-Fi screen: the networks in range (saved and current first), connection status and
// a rescan. Selecting a network is handled by the shell (password entry, background connect).
class WifiView {
public:
 enum class Action {None,Close,Rescan,Select};
 struct Hit {Action action{Action::None};int index{-1};};
 static constexpr int Rows=8;
 void open();
 void close()noexcept{active_=false;}
 bool update(std::vector<wifi::Network> networks,wifi::Status status,QString message,bool busy);
 bool active()const noexcept{return active_;}
 void paint(spike::Surface)const;
 Hit hit(int x,int y)const;
 const std::vector<wifi::Network>& networks()const noexcept{return networks_;}
 QStringList displayedText()const; /* exact text the screen shows (never a password) */
private:
 std::vector<wifi::Network> networks_;wifi::Status status_;QString message_;bool active_{},busy_{};
};
bool supported(const wifi::Network&);
}
namespace wifi {
struct Request {std::optional<Network> target;QString password;bool automatic{};};
// One Wi-Fi request runs at a time. A manual request made while a scan runs (automatic or opening the screen) waits for
// it, the newest replacing an older waiting one; a connection in progress refuses others; automatic passes never wait.
class Requests {
 bool busy_{},connecting_{};std::optional<Request> queued_;
public:
 enum class Decision {Start,Queued,Refused};
 Decision submit(const Request& r){if(!busy_){busy_=true;connecting_=bool(r.target);return Decision::Start;}if(r.automatic||connecting_)return Decision::Refused;queued_=r;return Decision::Queued;}
 std::optional<Request> finish(){busy_=connecting_=false;auto next=std::move(queued_);queued_.reset();return next;}
 bool busy()const noexcept{return busy_;}
 bool queued()const noexcept{return queued_.has_value();}
};
}
