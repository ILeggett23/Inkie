// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QByteArray>
#include <QString>
#include <atomic>
#include <optional>
#include <string>
#include <vector>
namespace wifi {
// Client of wpa_supplicant's control interface (the same interface wpa_cli uses). The tablet's
// wpa_supplicant runs as its own system service with the stock app's network list
// (update_config=1), so networks connected here are the ones the stock UI shows, and the reverse.
enum class Security {Open,Wep,Psk,Sae,PskSae,Enterprise};
struct Network {QString ssid;int signal{-100};Security security{Security::Open};int savedId{-1};bool current{};bool operator==(const Network&)const=default;};
struct Status {bool available{};QString state,ssid,ip;bool operator==(const Status&)const=default;bool connected()const{return state=="COMPLETED"&&!ip.isEmpty();}};
// Pure parsers of wpa_supplicant replies (tested without a socket).
Security security(const QByteArray& flags);
std::vector<Network> networks(const QByteArray& scanResults,const QByteArray& listNetworks,const QString& currentSsid);
Status status(const QByteArray& reply);
QString ssidText(const QByteArray& escaped); /* wpa_supplicant printf-escaped SSID to text */
QByteArray ssidHex(const QString& ssid); /* SSID as the unquoted hex form SET_NETWORK accepts */
class Control {
 std::string path_;
public:
 explicit Control(std::string socketPath="/var/run/wpa_supplicant/wlan0"):path_(std::move(socketPath)){}
 // One request/reply on a private abstract client socket; nullopt when the service is unreachable.
 std::optional<QByteArray> request(const QByteArray& command,int timeoutMs=3000,const std::atomic<bool>* stop=nullptr)const;
 Status status(const std::atomic<bool>* stop=nullptr)const;
 bool scan(const std::atomic<bool>* stop=nullptr)const;
 bool reconnect(const Status&,const std::atomic<bool>* stop=nullptr)const; // Only saved/enabled networks, never enables a disabled radio/network.
 std::vector<Network> networks(const std::atomic<bool>* stop=nullptr)const;
 // Connect to a saved network, or add the network with the password (and save it for the stock
 // app too) only once the connection succeeds. Returns an empty string on success, else a short
 // reason for the screen. Never leaves other saved networks disabled.
 QString connect(const Network&,const QString& password,int timeoutMs=25000,const std::atomic<bool>* stop=nullptr)const;
};
}
