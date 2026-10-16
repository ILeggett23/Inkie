// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <filesystem>
#include <stdexcept>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace release {
struct Policy {bool packaged{},networking{true};};
// Startup/worker boundary only. An absent file retains the explicitly unbundled
// reference harness; every offline package includes this checksummed policy.
inline Policy policy(const std::filesystem::path& app){
 auto path=app/"config/offline-release.json";
 int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0){if(errno==ENOENT)return {};throw std::runtime_error("release-policy-open");}
 struct Close{int fd;~Close(){close(fd);}} held{fd};struct stat st{};
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1||(st.st_mode&0022)||st.st_size<1||st.st_size>16384)throw std::runtime_error("release-policy-file");
 QByteArray bytes;char buffer[2048];for(;;){auto n=read(fd,buffer,sizeof buffer);if(n<0&&errno==EINTR)continue;if(n<0)throw std::runtime_error("release-policy-read");if(!n)break;bytes.append(buffer,int(n));if(bytes.size()>16384)throw std::runtime_error("release-policy-capacity");}
 QJsonParseError error;auto document=QJsonDocument::fromJson(bytes,&error);auto value=document.object();
 if(error.error!=QJsonParseError::NoError||!document.isObject()||value["schema"]!=1||value["id"]!="inkbound-reader-offline"||value["architecture"]!="aarch64"||value["entryPoint"]!="--offline-arm"||!value["connected"].isObject())throw std::runtime_error("release-policy-schema");
 const auto connected=value["connected"].toObject();
 for(const auto* key:{"enabled","liveInference","companionPolling","privateDraftAutoSend"})if(!connected[key].isBool()||connected[key].toBool())throw std::runtime_error("connected-release-not-qualified");
 return {true,false};
}
}
