#include "startup_lock.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <cstdio>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);if(getuid()!=0)return 77;
 QTemporaryDir tmp; if(!tmp.isValid())return 1;
 struct utsname host{};if(uname(&host))return 2;
 QFile b(QStringLiteral("/proc/sys/kernel/random/boot_id"));if(!b.open(QIODevice::ReadOnly))return 3;
 QByteArray boot=b.readAll().trimmed(),hostname(host.nodename);
 auto path=tmp.path()+"/lock";int count=0;
 auto check=[&](bool ok){if(!ok)std::abort();++count;};
 check(spike::prepareDisplayLock(path,hostname,boot));
 QLockFile live(path);live.setStaleLockTime(0);check(live.tryLock(0));
 check(!spike::prepareDisplayLock(path,hostname,boot));check(QFile::exists(path));live.unlock();
 int ready[2];if(pipe(ready))return 4;pid_t pid=fork();
 if(pid==0){close(ready[0]);QLockFile dead(path);dead.setStaleLockTime(0);char result=dead.tryLock(0)?'y':'n';(void)write(ready[1],&result,1);_exit(result=='y'?0:1);}
 if(pid<0)return 5;close(ready[1]);char result='n';check(read(ready[0],&result,1)==1&&result=='y');close(ready[0]);int status=0;check(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 check(!spike::prepareDisplayLock(path,"wrong-host",boot));check(QFile::exists(path));
 check(!spike::prepareDisplayLock(path,hostname,"wrong-boot"));check(QFile::exists(path));
 check(spike::prepareDisplayLock(path,hostname,boot));check(!QFile::exists(path));
 QFile malformed(path);check(malformed.open(QIODevice::WriteOnly));malformed.write("garbage\n");malformed.close();
 check(!spike::prepareDisplayLock(path,hostname,boot));check(QFile::exists(path));QFile::remove(path);
 auto target=tmp.path()+"/target";QFile f(target);check(f.open(QIODevice::WriteOnly));f.write("data");f.close();
 check(symlink(QFile::encodeName(target).constData(),QFile::encodeName(path).constData())==0);
 check(!spike::prepareDisplayLock(path,hostname,boot));
 std::printf("STARTUP_LOCK_TESTS_PASS assertions=%d vendor_calls=0 device_access=0\n",count);
}
