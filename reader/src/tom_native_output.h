// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "product_store.h"
#include "underlay.h"
#include <QJsonDocument>
#include <QStringList>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
namespace tomnative {
/* Derived pointers only. The durable native group and immutable Tom turn remain the source records. */
inline QJsonObject read(const std::filesystem::path& root,const QString& request)noexcept{try{if(!library::validId(request))return {};auto directory=root/"tom-native-output";struct stat parent{};if(lstat(directory.c_str(),&parent)||!S_ISDIR(parent.st_mode)||parent.st_uid!=getuid()||(parent.st_mode&0777)!=0700)return {};auto file=directory/(request.toStdString()+".json");struct stat st{};if(lstat(file.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1||(st.st_mode&0777)!=0600||st.st_size<=0||st.st_size>4096)return {};auto value=QJsonDocument::fromJson(underlay::readBounded(file,4096)).object();const QStringList keys{"document","group","note","request","revision","version"};if(value.keys()!=keys||value.value("version").toDouble()!=1||value.value("request")!=request||!library::validId(value.value("document").toString())||!library::validHash(value.value("revision").toString())||!library::validId(value.value("note").toString())||!library::validId(value.value("group").toString()))return {};return value;}catch(...){return {};}}
inline void remember(const std::filesystem::path& root,const QString& request,const QJsonObject& page,const QString& note,const QString& group){product::require(library::validId(request)&&library::validId(page.value("document").toString())&&library::validHash(page.value("revision").toString())&&library::validId(note)&&library::validId(group),"tom-native-output-binding");auto directory=root/"tom-native-output";underlay::privateDirectory(directory);auto file=directory/(request.toStdString()+".json");struct stat st{};if(!lstat(file.c_str(),&st))product::require(S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_nlink==1&&(st.st_mode&0777)==0600,"tom-native-output-file");else product::require(errno==ENOENT,"tom-native-output-stat");QJsonObject link{{"version",1},{"request",request},{"document",page.value("document")},{"revision",page.value("revision")},{"note",note},{"group",group}};if(read(root,request)==link)return;underlay::atomicFile(file,QJsonDocument(link).toJson(QJsonDocument::Compact));}
}
