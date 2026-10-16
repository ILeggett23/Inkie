// SPDX-License-Identifier: AGPL-3.0-only
#include "passcode.h"
#include "underlay.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <system_error>
namespace security {namespace {
constexpr int Iterations=20000;
std::filesystem::path file(const std::filesystem::path& root){return root/"ui-passcode.json";}
}
bool validCode(const QString& code)noexcept{if(code.size()!=PasscodeLength)return false;for(QChar ch:code)if(ch<QChar('0')||ch>QChar('9'))return false;return true;}
QByteArray derive(const QString& code,const QByteArray& salt,int iterations){
 const QByteArray secret=code.toLatin1();QByteArray h=QCryptographicHash::hash(salt+secret,QCryptographicHash::Sha256);
 for(int i=1;i<iterations;++i)h=QCryptographicHash::hash(h+salt+secret,QCryptographicHash::Sha256);
 return h;}
std::optional<Passcode> load(const std::filesystem::path& root)noexcept{
 try{std::error_code ec;if(!std::filesystem::exists(file(root),ec))return std::nullopt;
  const auto o=QJsonDocument::fromJson(underlay::readBounded(file(root),4096)).object();if(o["version"]!=1)return std::nullopt;
  Passcode p{QByteArray::fromHex(o["salt"].toString().toLatin1()),QByteArray::fromHex(o["digest"].toString().toLatin1()),o["iterations"].toInt()};
  if(p.salt.size()!=16||p.digest.size()!=32||p.iterations<1000||p.iterations>1000000)return std::nullopt;return p;}catch(...){return std::nullopt;}}
void save(const std::filesystem::path& root,const QString& code){
 if(!validCode(code))throw std::runtime_error("passcode-format");QByteArray salt(16,Qt::Uninitialized);for(auto& b:salt)b=char(QRandomGenerator::system()->bounded(256));
 underlay::atomicFile(file(root),QJsonDocument(QJsonObject{{"version",1},{"salt",QString::fromLatin1(salt.toHex())},{"digest",QString::fromLatin1(derive(code,salt,Iterations).toHex())},{"iterations",Iterations}}).toJson(QJsonDocument::Compact));}
void clear(const std::filesystem::path& root){std::error_code ec;std::filesystem::remove(file(root),ec);if(ec)throw std::runtime_error("passcode-clear");}
bool matches(const Passcode& p,const QString& code)noexcept{
 try{if(!validCode(code))return false;const auto d=derive(code,p.salt,p.iterations);if(d.size()!=p.digest.size())return false;unsigned char diff=0;for(int i=0;i<d.size();++i)diff|=static_cast<unsigned char>(d[i]^p.digest[i]);return diff==0;}catch(...){return false;}}
}
