// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "notes_runtime.h"
#include "ib/sheets.h"
namespace notes {
// Layers sheet (concept #8): top-most layer first, the protected document last.
class Layers final {
 unsigned offset_{};QString selected_;bool confirmDelete_{};int menuFor_{-1};
public:
 void paint(spike::Surface,const View&,ib::Targets&,std::string status={});
 // id is a target recorded by paint; returns a runtime op or a ui.* request.
 QJsonObject finger(int id,const View&);
 QString selected()const{return selected_;}void select(QString id){selected_=std::move(id);confirmDelete_=false;}
};
}
