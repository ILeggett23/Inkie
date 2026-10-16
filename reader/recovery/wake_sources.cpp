// SPDX-License-Identifier: AGPL-3.0-only
#include "wake_sources.h"
#include <charconv>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string_view>
namespace recovery::v2::sessionpower {namespace {
bool text(const std::filesystem::path& path,std::array<char,65>& bytes,std::size_t& length)noexcept{int fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return false;auto n=::read(fd,bytes.data(),bytes.size());::close(fd);if(n<=0||n>=ssize_t(bytes.size()))return false;length=std::size_t(n);if(bytes[length-1]=='\n')--length;return length>0;}
constexpr std::array<std::string_view,2> Names{"RM_HWMON_WS_DOBBEL_TAP","RM_HWMON_WS_UNDOCKED_PEN"};
}
bool NativeWakeSources::read(const Counter& counter,std::uint64_t& result)const noexcept{try{if(counter.path.empty()||std::filesystem::canonical(counter.path/"device")!=std::filesystem::canonical(root_/NativeHwmonDevice))return false;std::array<char,65> bytes{};std::size_t n=0;if(!text(counter.path/"event_count",bytes,n)||n>20)return false;for(std::size_t i=0;i<n;++i)if(bytes[i]<'0'||bytes[i]>'9')return false;auto parsed=std::from_chars(bytes.data(),bytes.data()+n,result);return parsed.ec==std::errc{}&&parsed.ptr==bytes.data()+n;}catch(...){return false;}}
void NativeWakeSources::baseline()noexcept{for(auto& c:counters_)c={};try{std::array<unsigned,2> matches{};unsigned count=0;for(const auto& entry:std::filesystem::directory_iterator(root_/"class/wakeup")){if(++count>256){for(auto& c:counters_)c={};return;}std::array<char,65> bytes{};std::size_t n=0;if(!text(entry.path()/"name",bytes,n))continue;const std::string_view name(bytes.data(),n);for(unsigned i=0;i<Names.size();++i)if(name==Names[i]){++matches[i];counters_[i].path=entry.path();}}for(unsigned i=0;i<counters_.size();++i){auto& c=counters_[i];c.valid=matches[i]==1&&read(c,c.value);}}catch(...){for(auto& c:counters_)c={};}}
WakeCause NativeWakeSources::afterResume()noexcept{WakeCause cause=WakeCause::None;for(unsigned i=0;i<counters_.size();++i){auto& c=counters_[i];if(!c.valid)continue;std::uint64_t value=0;if(!read(c,value)||value<c.value){c.valid=false;continue;}if(value>c.value&&cause==WakeCause::None)cause=i?WakeCause::PenUndocked:WakeCause::DoubleTap;c.value=value;}return cause;}
std::uint64_t bootTime()noexcept{timespec t{};return clock_gettime(CLOCK_BOOTTIME,&t)?0:std::uint64_t(t.tv_sec)*1000000000ull+std::uint64_t(t.tv_nsec);}
bool DoubleTapWake::tap(unsigned contacts,bool still,int x,int y,std::uint64_t start,std::uint64_t end,std::uint64_t now)noexcept{if(contacts!=1||!still||x<0||x>=1620||y<0||y>=2160||!start||start<cutoff_||end<start||end-start>250000000ull||now<end||now-end>1000000000ull){first_=0;return false;}const int dx=x-x_,dy=y-y_;if(first_&&start>=first_&&start-first_<=400000000ull&&dx*dx+dy*dy<=80*80){first_=0;return true;}first_=end;x_=x;y_=y;return false;}
}
