#include "companion_view.h"
#include "ux_paint.h"
#include "ib/components.h"
#include <QDateTime>
#include <QRegularExpression>
#include <cmath>
namespace ux {
namespace {
constexpr spike::Rect CloseBox{1300,40,1550,170},PrimaryBox{70,1640,730,1820},SecondaryBox{890,1640,1550,1820};
double now(){return double(QDateTime::currentSecsSinceEpoch());}
bool date(const QJsonValue& v){return v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()>0&&v.toDouble()<253402300800.;}
QString buttonName(CompanionView::Action a){switch(a){case CompanionView::Action::StartPair:return "START PAIRING";case CompanionView::Action::PollPair:return "CHECK CLAIM";case CompanionView::Action::Approve:return "APPROVE";case CompanionView::Action::Reject:return "REJECT";case CompanionView::Action::Sync:return "SYNC INBOX";case CompanionView::Action::RetryTransfer:return "RETRY TRANSFER";case CompanionView::Action::Unpair:return "UNPAIR";default:return {};}}
QString stateLabel(const QString& s){if(s=="transfer-error")return "TRANSFER PAUSED - REVIEW AND RETRY";if(s=="tls-unavailable")return "SECURE CONNECTION SUPPORT UNAVAILABLE";if(s=="unpaired")return "NOT PAIRED";if(s=="pairing")return "WAITING FOR A CLAIM";if(s=="approval-required")return "CONFIRM THE CLAIMED ACCOUNT";if(s=="approval-uncertain-repair-required")return "APPROVAL UNCERTAIN - PAIR AGAIN";if(s=="paired")return "PAIRED";if(s=="transferring")return "RECEIVING DOCUMENTS";if(s.contains("storage-error"))return "STORAGE ERROR - CLOSE AND REOPEN";if(s.contains("expired")||s=="expired")return "PAIRING OR CREDENTIAL EXPIRED";if(s.contains("revoked"))return "PAIRING REVOKED";if(s.contains("rejected"))return "PAIRING REJECTED";return "COMPANION UNAVAILABLE";}
}
void CompanionView::open(const QJsonObject& status){active_=true;confirmUnpair_=false;update(status);}
void CompanionView::update(const QJsonObject& input){
 status_={};for(const auto* key:{"state","pairing_id","pairing_code","account","expires_at","next_poll","received","total","busy","retry_available"})if(input.contains(key))status_[key]=input[key];
 if(input.contains("error")||input["operation_failed"].toBool())status_["has_error"]=true;
 painted_=false;pending_=false;paintedPair_.clear();paintedAccount_.clear();
}
bool CompanionView::accountSupported()const{auto a=status_["account"].toString();if(a.isEmpty()||a.size()>254)return false;for(auto c:a){auto n=c.unicode();if(!((n>='a'&&n<='z')||(n>='A'&&n<='Z')||(n>='0'&&n<='9')||n=='@'||n=='.'||n=='+'||n=='_'||n=='-'||n=='/'))return false;}return true;}
bool CompanionView::livePair()const{return QRegularExpression("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\z").match(status_["pairing_id"].toString()).hasMatch()&&date(status_["expires_at"])&&status_["expires_at"].toDouble()>now();}
bool CompanionView::backoff()const{return date(status_["next_poll"])&&status_["next_poll"].toDouble()>now();}
bool CompanionView::repair()const{auto s=status_["state"].toString();return s=="unpaired"||s=="expired"||s.contains("expired")||s.contains("revoked")||s.contains("rejected")||s=="approval-uncertain-repair-required"||(!consumedPair_.isEmpty()&&status_["pairing_id"].toString()==consumedPair_)||((s=="pairing"||s=="approval-required"||s=="paired")&&date(status_["expires_at"])&&status_["expires_at"].toDouble()<=now());}
CompanionView::Action CompanionView::primary()const{
 auto s=status_["state"].toString();if(pending_||status_["busy"].toBool()||backoff()||s=="transferring"||s.contains("storage-error"))return Action::None;
 if(s=="transfer-error")return status_["retry_available"].toBool()?Action::RetryTransfer:Action::Sync;
 if(repair())return Action::StartPair;
 if(s=="pairing"&&livePair())return Action::PollPair;
 if(s=="approval-required"&&livePair()&&accountSupported())return Action::Approve;
 if(s=="paired"&&date(status_["expires_at"])&&status_["expires_at"].toDouble()>now())return Action::Sync;
 return Action::None;
}
CompanionView::Action CompanionView::secondary()const{auto s=status_["state"].toString();if(!pending_&&!status_["busy"].toBool()&&(s=="paired"||s=="transfer-error"))return Action::Unpair;return !pending_&&!status_["busy"].toBool()&&!backoff()&&!repair()&&livePair()&&(s=="pairing"||s=="approval-required")?Action::Reject:Action::None;}
QStringList CompanionView::displayedText()const{
 QStringList lines{"COMPANION","CLOSE","LIBRARY READING WORKS OFFLINE","CLOSE TO RETURN TO YOUR BOOKS"};if(!active_)return {};
 auto s=status_["state"].toString();lines<<stateLabel(s);
 auto code=status_["pairing_code"].toString();if((s=="pairing"||s=="approval-required")&&livePair()&&QRegularExpression("^[A-Z0-9]{8}\\z").match(code).hasMatch())lines<<"PAIRING CODE"<<code;
 if(!status_["account"].toString().isEmpty()){lines<<"CLAIMED ACCOUNT";if(accountSupported())lines<<status_["account"].toString();else lines<<"ACCOUNT CANNOT BE SHOWN ACCURATELY"<<"APPROVAL DISABLED - ACCOUNT NAME UNSUPPORTED";}
 if(date(status_["expires_at"]))lines<<"EXPIRES "+QDateTime::fromSecsSinceEpoch(qint64(status_["expires_at"].toDouble())).toUTC().toString("yyyy-MM-dd HH:mm:ss 'UTC'");
 if(s=="transferring"){double received=status_["received"].toDouble(-1),total=status_["total"].toDouble(-1);if(std::isfinite(received)&&std::isfinite(total)&&received>=0&&total>=received&&total<=64.*1024*1024)lines<<"RECEIVED "+QString::number(qint64(received))+" OF "+QString::number(qint64(total))+" BYTES";else lines<<"TRANSFER IN PROGRESS";}
 if(pending_||status_["busy"].toBool())lines<<"REQUEST IN PROGRESS";else if(backoff())lines<<"WAIT "+QString::number(qint64(std::ceil(status_["next_poll"].toDouble()-now())))+" SECONDS BEFORE RETRY";else if(status_["has_error"].toBool())lines<<"CONNECTION ERROR - TRY AGAIN LATER";
 if(!pending_&&!status_["busy"].toBool()&&(s=="approval-uncertain-repair-required"||(!consumedPair_.isEmpty()&&status_["pairing_id"].toString()==consumedPair_)))lines<<"START FRESH - DO NOT REPLAY APPROVAL";
 auto p=buttonName(primary()),q=buttonName(secondary());if(!p.isEmpty())lines<<p;if(!q.isEmpty())lines<<q;return lines;
}
void CompanionView::paint(spike::Surface surface){
 painted_=false;if(!active_||surface.width!=1620||surface.height!=2160||surface.stridePixels<1620||surface.pixels.size()<std::size_t(surface.stridePixels)*2160)return;
 namespace t=ib::theme;ib::Canvas c(surface);ib::Targets tg;auto u=[](const QString& v){auto b=v.toUtf8();return std::string(b.constData(),std::size_t(b.size()));};
 auto sentence=[&](QString v){v=v.toLower();if(!v.isEmpty())v[0]=v[0].toUpper();v.replace(" - "," \u2014 ");return u(v);};
 c.fill({0,0,1620,2160},t::Paper);
 ib::text::draw(c,{ib::Face::Subhead,ib::theme::Title.size},70.f,150.f,"Companion",t::InkPrimary);ib::iconButton(c,tg,1,CloseBox,ib::Icon::Close);
 ib::text::block(c,{ib::Face::Serif,ib::theme::Body.size},{70,210,1550,320},"Pair this reader with your companion account to receive documents from your phone or computer.",t::InkSecondary,48,2);
 auto s=status_["state"].toString();ib::pill(c,{70,350,70+int(ib::text::measure({ib::Face::Text,ib::theme::Caption.size},sentence(stateLabel(s))))+60,404},sentence(stateLabel(s)),s=="paired"||s=="transferring");
 auto code=status_["pairing_code"].toString();if((s=="pairing"||s=="approval-required")&&livePair()&&QRegularExpression("^[A-Z0-9]{8}\\z").match(code).hasMatch()){ib::text::draw(c,{ib::Face::Text,ib::theme::Meta.size},70.f,500.f,"Pairing code",t::InkSecondary);ib::text::draw(c,{ib::Face::Literal,ib::theme::Code.size,8},70.f,640.f,u(code),t::InkPrimary);}
 if(!status_["account"].toString().isEmpty()){ib::text::draw(c,{ib::Face::Text,ib::theme::Meta.size},70.f,730.f,"Claimed account",t::InkSecondary);
  // Shown literally (supported ASCII only) so approval names the exact account.
  if(accountSupported()){auto account=status_["account"].toString();for(int i=0;i<account.size();i+=48)ib::text::draw(c,{ib::Face::Literal,ib::theme::Subhead.size},70.f,float(800+(i/48)*56),u(account.mid(i,48)),t::InkPrimary);} /* Literal: never script */
  else ib::text::block(c,{ib::Face::Serif,ib::theme::Body.size},{70,770,1550,900},"This account name can't be shown accurately, so approval is disabled.",t::InkPrimary,46,2);}
 for(const auto& line:displayedText()){
  // Shown in plain terms: minutes left rather than a UTC timestamp, and a percentage rather than byte counts.
  if(line.startsWith("EXPIRES ")){const qint64 left=qint64(std::ceil((status_["expires_at"].toDouble()-now())/60.));ib::text::draw(c,{ib::Face::Text,ib::theme::Meta.size},70.f,1180.f,left<=0?std::string("This code has expired"):left==1?std::string("Expires in 1 minute"):"Expires in "+std::to_string(left)+" minutes",t::InkPrimary);}
  else if(line.startsWith("RECEIVED ")){const double received=status_["received"].toDouble(),total=status_["total"].toDouble();const int pct=total>0?int(std::lround(100.*received/total)):0;ib::text::draw(c,{ib::Face::Serif,ib::theme::Body.size},70.f,1320.f,"Receiving a document \u2014 "+std::to_string(pct)+"%",t::InkPrimary);}
  else if(line=="TRANSFER IN PROGRESS")ib::text::draw(c,{ib::Face::Serif,ib::theme::Body.size},70.f,1320.f,"Receiving a document\u2026",t::InkPrimary);
  else if(line.startsWith("WAIT ")||line=="REQUEST IN PROGRESS"||line.startsWith("CONNECTION ERROR"))ib::text::draw(c,{ib::Face::Serif,ib::theme::Body.size},70.f,1440.f,sentence(line),line.startsWith("CONNECTION")?t::InkPrimary:t::InkSecondary);
  else if(line.startsWith("START FRESH"))ib::text::draw(c,{ib::Face::Serif,ib::theme::Body.size},70.f,1530.f,"Start fresh — an earlier approval will not be replayed",t::InkPrimary);
 }
 const Action paintedPrimary=primary(),paintedSecondary=secondary();auto p=buttonName(paintedPrimary),q=buttonName(paintedSecondary);
 if(!p.isEmpty())ib::button(c,tg,2,ib::inset(PrimaryBox,20),sentence(p),ib::ButtonKind::Primary);
 if(!q.isEmpty())ib::button(c,tg,3,ib::inset(SecondaryBox,20),sentence(paintedSecondary==Action::Unpair&&confirmUnpair_?QString("UNPAIR THIS TABLET?"):q),ib::ButtonKind::Secondary);
 ib::text::draw(c,{ib::Face::TextItalic,ib::theme::Meta.size},70.f,2000.f,"Reading in your library always works offline.",t::InkTertiary);
 paintedPair_=status_["pairing_id"].toString();paintedAccount_=status_["account"].toString();paintedPrimary_=paintedPrimary;paintedSecondary_=paintedSecondary;painted_=true;
}
CompanionView::Hit CompanionView::hit(int x,int y){
 if(!active_||x<0||x>=1620||y<0||y>=2160)return {};
 if(CloseBox.contains(x,y)){active_=false;painted_=false;return{Action::Close,{},{}};}
 if(!painted_||pending_)return {};Action a=PrimaryBox.contains(x,y)?paintedPrimary_:SecondaryBox.contains(x,y)?paintedSecondary_:Action::None;
 const Action current=PrimaryBox.contains(x,y)?primary():SecondaryBox.contains(x,y)?secondary():Action::None;
 if(a!=current){painted_=false;return {Action::Changed,{},{}};}
 if(a==Action::None)return {};
 if(a==Action::Unpair&&!confirmUnpair_){confirmUnpair_=true;painted_=false;return {Action::Changed,{},{}};} /* second tap confirms */
 confirmUnpair_=false;
 Hit result{a,{}, {}};priorConsumed_=consumedPair_;if(a==Action::Approve||a==Action::Reject){result.expectedPairId=paintedPair_;result.expectedAccount=paintedAccount_;consumedPair_=paintedPair_;}
 pending_=true;painted_=false;return result;
}
void CompanionView::refused(){pending_=false;painted_=false;consumedPair_=priorConsumed_;}
}
