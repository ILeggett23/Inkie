// SPDX-License-Identifier: AGPL-3.0-only
#include "wifi_view.h"
#include "ux_paint.h"
#include "ib/components.h"
#include "ib/text.h"
#include "ib/theme.h"
#include "ib/type.h"
namespace ux {
namespace {
constexpr spike::Rect CloseBox{1300,40,1550,170},RescanBox{70,1880,730,2040};
spike::Rect row(int n){const int y=420+n*175;return {60,y,1560,y+150};}
QString shown(const QString& ssid){return ssid.size()>34?ssid.left(32)+QStringLiteral("\u2026"):ssid;}
QString kind(const wifi::Network& n){
 if(n.current)return "CONNECTED";if(!supported(n))return "NOT SUPPORTED HERE";if(n.savedId>=0)return "SAVED";
 return n.security==wifi::Security::Open?"OPEN":"PASSWORD";
}
QString headline(const wifi::Status& s){
 if(!s.available)return "WI-FI IS OFF - TURN IT ON IN THE REMARKABLE'S OWN SETTINGS";
 if(s.connected())return "CONNECTED TO "+shown(s.ssid);
 if(!s.ssid.isEmpty()&&s.state=="COMPLETED")return "CONNECTED TO "+shown(s.ssid)+" - WAITING FOR AN ADDRESS";
 return "NOT CONNECTED";
}
}
bool supported(const wifi::Network& n){return n.security!=wifi::Security::Wep&&n.security!=wifi::Security::Enterprise;}
void WifiView::open(){active_=true;busy_=true;message_="SCANNING";networks_.clear();}
bool WifiView::update(std::vector<wifi::Network> networks,wifi::Status status,QString message,bool busy){if(networks.size()>std::size_t(Rows))networks.resize(Rows);const bool changed=networks_!=networks||status_!=status||message_!=message||busy_!=busy;networks_=std::move(networks);if(networks_.size()>std::size_t(Rows))networks_.resize(Rows);status_=std::move(status);message_=std::move(message);busy_=busy;return changed;}
QStringList WifiView::displayedText()const{
 if(!active_)return {};QStringList out{"WI-FI","CLOSE",headline(status_)};if(!message_.isEmpty())out<<message_;
 for(const auto& n:networks_)out<<shown(n.ssid)<<kind(n);
 if(status_.available&&networks_.empty()&&!busy_)out<<"NO NETWORKS FOUND";
 out<<"RESCAN"<<"NETWORKS ARE SHARED WITH THE REMARKABLE'S OWN WI-FI SETTINGS";return out;
}
void WifiView::paint(spike::Surface s)const{
 if(!active_||s.width!=1620||s.height!=2160)return;
 namespace t=ib::theme;ib::Canvas c(s);ib::Targets tg;auto u=[](const QString& v){auto b=v.toUtf8();return std::string(b.constData(),std::size_t(b.size()));};
 auto sentence=[&](QString v){if(v.startsWith("WI-FI ERROR"))v="Wi-Fi didn\u2019t respond \u2014 try again";else{v=v.toLower();if(!v.isEmpty())v[0]=v[0].toUpper();v.replace("wi-fi","Wi-Fi",Qt::CaseInsensitive);v.replace(" - "," \u2014 ");}return u(v);};
 // Network names are shown exactly as broadcast; only the fixed labels are sentence-cased.
 auto status=[&]()->std::string{if(!status_.available)return "Wi-Fi is off. Turn it on in the reMarkable\u2019s own settings.";if(status_.connected())return "Connected to "+u(shown(status_.ssid));if(!status_.ssid.isEmpty()&&status_.state=="COMPLETED")return "Connected to "+u(shown(status_.ssid))+", waiting for an address";return "Not connected";};
 c.fill({0,0,1620,2160},t::Paper);
 ib::text::draw(c,{ib::Face::Subhead,ib::theme::Title.size},70.f,150.f,"Wi-Fi",t::InkPrimary);ib::iconButton(c,tg,1,{CloseBox.x1-ib::theme::MinTarget,CloseBox.y0+17,CloseBox.x1,CloseBox.y1-17},ib::Icon::Close); /* the close sits where every Inkie sheet has it; the whole box still takes the tap */
 ib::text::draw(c,{ib::Face::Serif,ib::theme::Body.size},70.f,270.f,status(),t::InkPrimary);
 if(!message_.isEmpty()){auto m=message_;const bool named=m.startsWith("CONNECTING TO ");ib::text::draw(c,{ib::Face::TextItalic,ib::theme::Control.size},70.f,340.f,named?"Connecting to "+u(m.mid(14)):sentence(m),t::InkSecondary);}
 // Rows: the network in use is an ink row (state is ink); a usable one is a card with an ink rule; one this tablet
 // can't join has a dotted outline. Signal bars are solid up to the level and hollow above it.
 for(int n=0;n<int(networks_.size());++n){const auto& net=networks_[std::size_t(n)];const auto r=row(n);const bool ok=supported(net);
  const std::uint32_t ink=net.current?t::OnSelected:t::InkPrimary;
  if(net.current)c.fillRound(r,t::RadiusMedium,t::ToolSelected);else if(ok)ib::panel(c,r,t::RadiusMedium);else{c.fillRound(r,t::RadiusMedium,t::Surface);ib::divider(c,r.x0+12,r.x1-12,r.y0);ib::divider(c,r.x0+12,r.x1-12,r.y1-2);}
  ib::text::draw(c,{ib::Face::Literal,ib::theme::Subhead.size},100.f,float(r.y0+66),ib::text::fit({ib::Face::Literal,ib::theme::Subhead.size},u(shown(net.ssid)),1260),ink); /* exactly as broadcast: never script */
  ib::text::draw(c,{ib::Face::Text,ib::theme::Meta.size},100.f,float(r.y0+116),sentence(kind(net)),ink);
  if(ok){const int level=net.signal>=-55?4:net.signal>=-67?3:net.signal>=-78?2:1;for(int b=0;b<4;++b){ib::Rect q{1400+b*30,r.y0+100-(b+1)*15,1400+b*30+20,r.y0+100};if(b<level)c.fillRound(q,3,ink);else c.strokeRound(q,3,2.f,ink);}}}
 if(status_.available&&networks_.empty()&&!busy_)ib::text::draw(c,ib::type::ScriptEmpty,70.f,520.f,"No networks found",t::InkSecondary);
 ib::button(c,tg,2,ib::inset(RescanBox,20),busy_?"Please wait":"Rescan",ib::ButtonKind::Secondary,busy_,ib::Icon::Rotate);
 ib::text::block(c,{ib::Face::Text,ib::theme::Meta.size},{800,1915,1550,2030},"Networks are shared with the reMarkable\u2019s own Wi-Fi settings.",t::InkPrimary,36,2);
}
WifiView::Hit WifiView::hit(int x,int y)const{
 if(!active_)return {};if(CloseBox.contains(x,y))return {Action::Close,-1};if(busy_)return {};
 if(RescanBox.contains(x,y))return {Action::Rescan,-1};
 for(int n=0;n<int(networks_.size());++n)if(row(n).contains(x,y)){const auto& net=networks_[std::size_t(n)];if(net.current||!supported(net))return {};return {Action::Select,n};}
 return {};
}
}
