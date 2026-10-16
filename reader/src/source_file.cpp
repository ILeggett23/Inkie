#include "source_file.h"
#include "durable_file.h"
#include <QCryptographicHash>
#include <QByteArrayView>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace reader::source {namespace {
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct File {int fd{-1};explicit File(const std::filesystem::path& p):fd(open(p.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)){need(fd>=0,"source-file-open");}~File(){if(fd>=0)close(fd);}File(const File&)=delete;File& operator=(const File&)=delete;};
Identity identity(int fd){struct stat st{};need(fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_size>=0,"source-file-type");return {std::uint64_t(st.st_dev),std::uint64_t(st.st_ino),std::uint64_t(st.st_size),st.st_mtim.tv_sec,st.st_mtim.tv_nsec,st.st_ctim.tv_sec,st.st_ctim.tv_nsec};}
ssize_t readSome(int fd,char* data,std::size_t capacity){ssize_t n;do{n=read(fd,data,capacity);}while(n<0&&errno==EINTR);need(n>=0,"source-file-read");return n;}
void writeAll(int fd,const char* data,std::size_t size){for(std::size_t at=0;at<size;){auto n=write(fd,data+at,size-at);if(n<0&&errno==EINTR)continue;need(n>0,"source-file-copy-write");at+=std::size_t(n);}}
}
HashedFile hashFile(const std::filesystem::path& path,std::uint64_t maximum,const Progress& progress){File file(path);const auto before=identity(file.fd);need(before.size<=maximum&&before.size<=MaxDocumentBytes,"source-file-size");QCryptographicHash hash(QCryptographicHash::Sha256);std::array<char,256*1024> buffer;std::uint64_t count=0;for(;;){const auto n=readSome(file.fd,buffer.data(),buffer.size());if(!n)break;need(std::uint64_t(n)<=before.size-count,"source-file-grew");hash.addData(QByteArrayView(buffer.data(),qsizetype(n)));count+=std::uint64_t(n);if(progress)progress(std::uint64_t(n));}need(count==before.size&&identity(file.fd)==before,"source-file-changed");return {hash.result(),before};}
void copyFile(const std::filesystem::path& path,const std::filesystem::path& destination,const HashedFile& checked,const Progress& progress){File file(path);need(identity(file.fd)==checked.identity,"source-file-changed-before-copy");storage::replaceDurablyGenerated(destination,[&](int output){std::array<char,256*1024> buffer;std::uint64_t count=0;for(;;){const auto n=readSome(file.fd,buffer.data(),buffer.size());if(!n)break;need(std::uint64_t(n)<=checked.identity.size-count,"source-file-grew");writeAll(output,buffer.data(),std::size_t(n));count+=std::uint64_t(n);if(progress)progress(std::uint64_t(n));}need(count==checked.identity.size&&identity(file.fd)==checked.identity,"source-file-changed-during-copy");});}
}
