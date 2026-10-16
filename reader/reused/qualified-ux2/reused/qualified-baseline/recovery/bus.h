// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "contract.h"
#include <systemd/sd-bus.h>
#include <vector>
namespace recovery {
struct Property {std::string name,type,value;std::uint64_t number{};};
class Bus {
 sd_bus*bus_{};
public:
 Bus();~Bus();Bus(const Bus&)=delete;
 bool alive()const;void disconnect();
 std::string call(const std::string&method,const std::string&unit,bool mode=false);
 State state(const std::string&unit);
 void transient(const std::string&unit,const std::vector<Property>&,const std::vector<std::string>&command,const std::vector<std::string>&post={});
};
}
