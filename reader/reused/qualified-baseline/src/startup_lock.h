#pragma once
#include <QLockFile>
#include <QFile>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <climits>
namespace spike {
// Called only after the existing owner-issued stopped-stock permit. Never use
// file age, process name alone, or a blind unlink to decide ownership.
inline bool prepareDisplayLock(const QString& path,const QByteArray& hostname,const QByteArray& boot){
 const QByteArray native=QFile::encodeName(path);struct stat before{},after{};
 errno=0;if(lstat(native.constData(),&before)<0)return errno==ENOENT;
 if(!S_ISREG(before.st_mode)||before.st_uid!=0||(before.st_mode&0022)||before.st_size<=0||before.st_size>512)return false;
 QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;
 if(fstat(file.handle(),&after)||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino)return false;
 const auto bytes=file.read(513);file.close();if(bytes.size()!=before.st_size)return false;
 const auto lines=bytes.split('\n');if(lines.size()<5||lines[2]!=hostname||lines[4]!=boot)return false;
 bool numeric=false;const auto pid=lines[0].toLongLong(&numeric);
 if(!numeric||pid<=1||pid>INT_MAX)return false;
 errno=0;if(kill(static_cast<pid_t>(pid),0)==0||errno!=ESRCH)return false;
 if(lstat(native.constData(),&after)||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino||before.st_size!=after.st_size)return false;
 QLockFile lock(path);lock.setStaleLockTime(0); // Never discard an aged live lock.
 // Qt's bounded atomic lock protocol resolves stale ownership; no manual unlink.
 if(!lock.tryLock(0))return false;
 lock.unlock();
 errno=0;return lstat(native.constData(),&after)<0&&errno==ENOENT;
}
}
