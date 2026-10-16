// SPDX-License-Identifier: AGPL-3.0-only
#include "contract.h"
#include <stdexcept>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <dirent.h>
#include <poll.h>
#include <time.h>
#include <sstream>
namespace recovery {
Ns now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))throw std::runtime_error("clock");return Ns(t.tv_sec)*1000000000+Ns(t.tv_nsec);}
bool hex32(const std::string&s){return s.size()==32&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string nonce(){unsigned char b[16];if(getrandom(b,sizeof(b),0)!=sizeof(b))throw std::runtime_error("entropy");std::string s;for(auto v:b){s+="0123456789abcdef"[v>>4];s+="0123456789abcdef"[v&15];}return s;}
void copy(char*d,std::size_t n,const std::string&s){if(s.size()>=n)throw std::runtime_error("string bound");std::memset(d,0,n);std::memcpy(d,s.data(),s.size());}
std::string readSmall(const std::string&p,std::size_t max){int fd=open(p.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)throw std::runtime_error("read open "+p);std::string s(max+1,'\0');ssize_t n=read(fd,s.data(),s.size());close(fd);if(n<0||std::size_t(n)>max)throw std::runtime_error("read bound "+p);s.resize(n);return s;}
std::string boot(){auto s=readSmall("/proc/sys/kernel/random/boot_id",64);if(!s.empty()&&s.back()=='\n')s.pop_back();if(s.size()!=36)throw std::runtime_error("boot");return s;}
std::string executable(){char s[4096];auto n=readlink("/proc/self/exe",s,sizeof(s)-1);if(n<=0||n>=4095)throw std::runtime_error("self path");s[n]=0;return s;}
void writeFile(const std::string&p,const std::string&text){int fd=open(p.c_str(),O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)throw std::runtime_error("journal");auto n=write(fd,text.data(),text.size());if(n!=ssize_t(text.size())){close(fd);throw std::runtime_error("journal write");}fsync(fd);close(fd);}
static bool cleanPath(const std::string&p){if(p.empty()||p[0]!='/'||p.size()>512||p.back()=='/'||p.find("//")!=p.npos)return false;std::istringstream in(p);std::string part;while(std::getline(in,part,'/'))if(part=="."||part==".."||part.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-")!=part.npos)return false;return true;}
bool validBinding(const Binding&b){
 if(!memchr(b.session,0,sizeof b.session)||!memchr(b.boot,0,sizeof b.boot)||!memchr(b.unit,0,sizeof b.unit)||!memchr(b.owner,0,sizeof b.owner)||!memchr(b.invocation,0,sizeof b.invocation)||!memchr(b.ownerInvocation,0,sizeof b.ownerInvocation)||!memchr(b.group,0,sizeof b.group)||!memchr(b.mount,0,sizeof b.mount))return false;
 return hex32(b.session)&&std::strlen(b.boot)==36&&b.unit==childUnit(b.session)&&b.owner==ownerUnit(b.session)&&hex32(b.invocation)&&hex32(b.ownerInvocation)&&cleanPath(b.group)&&std::string(b.group)=="/system.slice/"+childUnit(b.session)&&(std::string(b.mount)=="/sys/fs/cgroup"||std::string(b.mount)=="/sys/fs/cgroup/unified")&&b.device&&b.inode&&b.ownerPid>1&&b.mainPid>1&&b.deadline&&std::any_of(b.ticket.begin(),b.ticket.end(),[](auto x){return x!=0;});
}
bool terminal(const State&s){return s.load=="loaded"&&((s.active=="inactive"&&s.sub=="dead")||(s.active=="failed"&&s.sub=="failed"))&&s.main==0&&s.control==0&&s.job==0&&s.group.empty();}
bool complete(const Binding&b,const Packet&r,const State&s,bool reference,bool closed,bool absent,bool authenticated){return validBinding(b)&&reference&&closed&&absent&&authenticated&&r.magic==0x33424e49&&r.version==1&&r.kind==Kind::Receipt&&r.closed==1&&r.drained==1&&same(b,r.binding)&&s.boot==b.boot&&s.id==b.unit&&s.invocation==b.invocation&&terminal(s);}
bool sendPacket(int fd,const Packet&p){return send(fd,&p,sizeof p,MSG_NOSIGNAL)==sizeof p;}
bool receivePacket(int fd,Packet&p,int ms){pollfd f{fd,POLLIN,0};if(poll(&f,1,ms)<=0||!(f.revents&POLLIN))return false;auto n=recv(fd,&p,sizeof p,MSG_TRUNC);return n==sizeof p&&p.magic==0x33424e49&&p.version==1&&memchr(p.binding.session,0,sizeof p.binding.session)&&memchr(p.binding.boot,0,sizeof p.binding.boot)&&memchr(p.binding.unit,0,sizeof p.binding.unit)&&memchr(p.binding.owner,0,sizeof p.binding.owner)&&memchr(p.binding.invocation,0,sizeof p.binding.invocation)&&memchr(p.binding.ownerInvocation,0,sizeof p.binding.ownerInvocation)&&memchr(p.binding.group,0,sizeof p.binding.group)&&memchr(p.binding.mount,0,sizeof p.binding.mount);}
pid_t peer(int fd){ucred c{};socklen_t n=sizeof c;if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&c,&n)||n!=sizeof c||c.uid!=0)return -1;return c.pid;}
bool connected(int fd){char c;auto n=recv(fd,&c,1,MSG_PEEK|MSG_DONTWAIT);return n>0||(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK));}
int listenOwner(const std::string&s){sockaddr_un a{};a.sun_family=AF_UNIX;auto p=socketPath(s);if(p.size()>=sizeof a.sun_path)throw std::runtime_error("socket path");std::memcpy(a.sun_path,p.c_str(),p.size()+1);int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(fd<0||bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof a)||chmod(p.c_str(),0600)||listen(fd,4)){if(fd>=0)close(fd);throw std::runtime_error("listen");}return fd;}
int connectOwner(const std::string&s,int ms){if(!hex32(s))return -1;sockaddr_un a{};a.sun_family=AF_UNIX;auto p=socketPath(s);if(p.size()>=sizeof a.sun_path)return -1;std::memcpy(a.sun_path,p.c_str(),p.size()+1);Ns end=now()+Ns(ms)*1000000;do{int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(fd<0)return -1;if(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof a)==0)return fd;close(fd);usleep(10000);}while(now()<end);return -1;}
std::string managerMount(){auto text=readSmall("/proc/self/mountinfo",65536);std::istringstream in(text);std::string l,v2;unsigned n=0,count=0;bool legacy=false;while(std::getline(in,l)){if(++n>256)throw std::runtime_error("mount count");auto split=l.find(" - ");if(split==l.npos)throw std::runtime_error("mount syntax");std::istringstream left(l.substr(0,split)),right(l.substr(split+3));std::string id,parent,dev,root,point,opts,type,source,super;left>>id>>parent>>dev>>root>>point>>opts;right>>type>>source>>super;if(type=="cgroup2"){if(root!="/")throw std::runtime_error("mount namespace");v2=point;++count;}if(type=="cgroup"&&super.find("name=systemd")!=super.npos)legacy=point=="/sys/fs/cgroup/systemd";}
 if(count!=1||!((v2=="/sys/fs/cgroup"&&!legacy)||(v2=="/sys/fs/cgroup/unified"&&legacy)))throw std::runtime_error("manager hierarchy");return v2;}
bool absentGroup(const Binding&b){if(managerMount()!=b.mount)return false;struct stat s{};errno=0;return lstat((std::string(b.mount)+b.group).c_str(),&s)<0&&errno==ENOENT;}
static bool scan(int fd,pid_t own,std::uint64_t dev,unsigned depth,unsigned&groups){
 if(depth>8||++groups>64)return false;struct stat before{},after{};if(fstat(fd,&before)||!S_ISDIR(before.st_mode)||before.st_dev!=dev)return false;
 int members=openat(fd,"cgroup.procs",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(members<0)return false;struct stat ms{};char bytes[8193];auto n=read(members,bytes,sizeof bytes);bool readable=!fstat(members,&ms)&&(ms.st_mode&0444);close(members);if(!readable||n<0||n==sizeof bytes)return false;std::string first(bytes,n);std::istringstream lines(first);std::string line;while(std::getline(lines,line)){if(line.empty()||line.find_first_not_of("0123456789")!=line.npos||line.size()>10||std::stoul(line)!=unsigned(own))return false;}
 DIR*d=fdopendir(dup(fd));if(!d)return false;unsigned entries=0;bool ok=true;errno=0;while(auto*e=readdir(d)){if(++entries>256){ok=false;break;}if(!std::strcmp(e->d_name,".")||!std::strcmp(e->d_name,".."))continue;struct stat st{};if(fstatat(fd,e->d_name,&st,AT_SYMLINK_NOFOLLOW)||S_ISLNK(st.st_mode)){ok=false;break;}if(S_ISDIR(st.st_mode)){int child=openat(fd,e->d_name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(child<0||!scan(child,own,dev,depth+1,groups))ok=false;if(child>=0)close(child);if(!ok)break;}errno=0;}if(errno)ok=false;closedir(d);
 members=openat(fd,"cgroup.procs",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(members<0)return false;n=read(members,bytes,sizeof bytes);close(members);return ok&&n>=0&&n<ssize_t(sizeof bytes)&&std::string(bytes,n)==first&&!fstat(fd,&after)&&after.st_ino==before.st_ino&&after.st_dev==before.st_dev;
}
bool inspectSubtree(int fd,pid_t own,std::uint64_t dev){try{unsigned groups=0;return scan(fd,own,dev,0,groups);}catch(...){return false;}}
static int directoryChain(const std::string&p){if(!cleanPath(p))return -1;int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);std::istringstream parts(p);std::string part;while(std::getline(parts,part,'/')){if(part.empty())continue;int next=openat(fd,part.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);close(fd);if(next<0)return -1;fd=next;}return fd;}
bool membershipWitness(const Binding&b,pid_t own){try{if(!validBinding(b)||boot()!=b.boot||managerMount()!=b.mount)return false;auto membership=readSmall("/proc/self/cgroup");if(membership.find("0::"+std::string(b.group)+"\n")==membership.npos)return false;int fd=directoryChain(std::string(b.mount)+b.group);if(fd<0)return false;struct stat st{};unsigned groups=0;bool ok=!fstat(fd,&st)&&st.st_dev==b.device&&st.st_ino==b.inode&&scan(fd,own,b.device,0,groups);close(fd);return ok;}catch(...){return false;}}
}
