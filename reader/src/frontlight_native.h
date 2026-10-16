#pragma once
#include <string>
#include <charconv>
#include <fcntl.h>
#include <unistd.h>
#include <sys/vfs.h>
#include <linux/magic.h>
namespace device::frontlight {
struct State {bool available{};int requested{-1},actual{-1},maximum{-1},power{-1};};
/* Only the native kernel backlight API. No user files, startup write or automatic power policy. */
class Native {
 int directory_{-1};
 int number(const char* name)const noexcept{if(directory_<0)return -1;int fd=openat(directory_,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;char bytes[32]{};auto n=::read(fd,bytes,sizeof(bytes));close(fd);if(n<=0||n>=ssize_t(sizeof(bytes)))return -1;while(n>0&&(bytes[n-1]=='\n'||bytes[n-1]==' '))--n;int value{};auto parsed=std::from_chars(bytes,bytes+n,value);return parsed.ec==std::errc{}&&parsed.ptr==bytes+n?value:-1;}
 bool writeNumber(const char* name,int value)const noexcept{int fd=openat(directory_,name,O_WRONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return false;char bytes[32];auto formatted=std::to_chars(bytes,bytes+sizeof(bytes)-1,value);if(formatted.ec!=std::errc{}){close(fd);return false;}*formatted.ptr++='\n';const auto size=formatted.ptr-bytes;auto n=write(fd,bytes,size);
#ifdef INKBOUND_FRONTLIGHT_TEST_IO
 if(n==size)(void)ftruncate(fd,size);
#endif
 const int closed=close(fd);return n==size&&closed==0;}
public:
 Native()noexcept{directory_=open("/sys/class/backlight/rm_frontlight",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(directory_>=0){struct statfs fs{};if(fstatfs(directory_,&fs)||fs.f_type!=SYSFS_MAGIC){close(directory_);directory_=-1;}}}

#ifdef INKBOUND_FRONTLIGHT_TEST_IO
 explicit Native(int ownedTestDirectory)noexcept:directory_(dup(ownedTestDirectory)){}
#endif
 ~Native(){if(directory_>=0)close(directory_);}Native(const Native&)=delete;Native& operator=(const Native&)=delete;
 State read()const noexcept{State s;s.requested=number("brightness");s.actual=number("actual_brightness");s.maximum=number("max_brightness");s.power=number("bl_power");s.available=s.maximum>0&&s.maximum<=65535&&s.requested>=0&&s.requested<=s.maximum&&s.actual>=0&&s.actual<=s.maximum&&(s.power==0||s.power==4);return s;}
 bool set(int level)const noexcept{auto s=read();if(!s.available||level<0||level>s.maximum)return false;if(!writeNumber("brightness",level))return false;if(!writeNumber("bl_power",level?0:4))return false;auto after=read();return after.available&&after.requested==level&&after.power==(level?0:4);}
};
}
