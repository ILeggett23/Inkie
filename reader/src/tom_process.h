#pragma once
#include <filesystem>
#include <optional>
#include <chrono>
#include <QJsonObject>
#include <QString>
#include <sys/types.h>
namespace tom {
// Worker-owned, nonblocking child control. No content or credentials in argv.
class HelperProcess {
 std::filesystem::path application_,root_;pid_t pid_{-1};QString nonce_,action_,conversation_,turn_;bool killed_{};std::chrono::steady_clock::time_point deadline_{};
public:
 HelperProcess(std::filesystem::path application,std::filesystem::path root):application_(std::move(application)),root_(std::move(root)){}
 ~HelperProcess();bool start(QString action,QString conversation={},QString turn={});
 std::optional<QJsonObject> poll();void terminate()noexcept;bool running()const noexcept{return pid_>0;}
 const QString& action()const{return action_;}const QString& conversation()const{return conversation_;}const QString& turn()const{return turn_;}
};
}
