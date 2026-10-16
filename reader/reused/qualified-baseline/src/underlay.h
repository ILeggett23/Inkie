#pragma once
#include "session.h"
#include "vendor.h"
#include <QByteArray>
#include <QJsonObject>
#include <filesystem>
#include <thread>
#include <vector>
namespace underlay {
constexpr int Width=1620,Height=2160,Stride=1632;
constexpr std::size_t Pixels=std::size_t(Stride)*Height,MaxPoints=16384,Slots=4,MaxOperations=128;
inline constexpr const char* FixtureHash="932b941ae388000bb21041178c0b21ac46dc0e620e472dd320f13fb3735a5399";
inline constexpr const char* DataPath="/home/root/.local/share/inkbound/document-underlay1";
QByteArray digest(const QByteArray&);
QByteArray readBounded(const std::filesystem::path&,std::size_t);
void atomicFile(const std::filesystem::path&,const QByteArray&);
void privateDirectory(const std::filesystem::path&);
struct Prepared {
 QJsonObject metadata; QByteArray binding,journalDigest;
 std::vector<std::uint32_t> background,ink;
 std::uint64_t lastId{};
};
void compose(spike::Surface output,std::span<const std::uint32_t> background,spike::Surface ink,spike::Rect);
Prepared loadPrepared(const std::filesystem::path&);
void savePrepared(const std::filesystem::path&,Prepared&);
enum class SaveState:unsigned {Free,VisibleVolatile,Queued,Committed,Failed};
struct Operation {std::uint64_t id{};std::size_t count{};bool eraser{};std::array<spike::Point,MaxPoints> points{};std::atomic<SaveState> state{SaveState::Free};};
// Fault/delay injection belongs to host tests, never enabled by target CLI/env.
struct IoFault {int failWriteAfter{-1};bool failSync{},failDirectorySync{};unsigned delayMs{};int writeErrno{5};};
class Journal {
 int fd_{-1};std::filesystem::path path_;QByteArray binding_;std::array<QByteArray,MaxOperations> hashes_{};
 std::uint64_t last_{};std::size_t offset_{};IoFault fault_;
 void load(spike::Surface*);
 void checkPath()const;
public:
 Journal(const std::filesystem::path&,const QByteArray&,IoFault={});~Journal();
 Journal(const Journal&)=delete;
 void replay(spike::Surface);void append(const Operation&);
 std::uint64_t lastId()const{return last_;}QByteArray fingerprint()const;
};
class Pipeline final:public spike::OperationSink {
 std::unique_ptr<std::array<Operation,Slots>> slots_;spike::Spsc<unsigned,Slots+1> queue_;
 std::unique_ptr<Journal> journal_;std::thread worker_;std::atomic<bool> stop_{false},done_{false},failed_{false};
 int active_{-1};std::uint64_t next_{};IoFault fault_;
 void work()noexcept;
public:
 std::atomic<unsigned> committed{0},queued{0},highWater{0},rejected{0};
 Pipeline(const std::filesystem::path&,const Prepared&,IoFault={});~Pipeline();
 bool reserve()noexcept override;bool append(const spike::Point&)noexcept override;void cancel()noexcept override;
 bool finish(unsigned milliseconds=100)noexcept;
 unsigned unsaved()const noexcept;bool failed()const noexcept{return failed_.load();}
};
class ComposedDisplay final:public spike::SessionDisplay {
 spike::VendorDisplay& vendor_;Prepared& prepared_;Pipeline& pipe_;
public:
 ComposedDisplay(spike::VendorDisplay&,Prepared&,Pipeline&);
 bool initialize(const spike::RecoveryLease&)noexcept override;
 spike::Surface surface()const noexcept override;
 bool initialPage()noexcept override;
 spike::Submit submit(spike::Rect)noexcept override;
 void pump()noexcept override;void shutdown()noexcept override;
 bool diagnosticSegment()const noexcept override{return false;}
 spike::OperationSink* operationSink()noexcept override{return &pipe_;}
};
Prepared prepare(const std::filesystem::path& application,const std::filesystem::path& data);
int prepareSibling()noexcept;
}
