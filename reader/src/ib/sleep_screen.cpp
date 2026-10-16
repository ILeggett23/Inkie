// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/sleep_screen.h"
namespace ib::sleep {
void paint(Canvas& c){c.fill({0,0,1620,2160},theme::Paper);text::centeredLine(c,{Face::Text,42},{180,104,1440,200},"Inkbound",theme::InkPrimary,Align::Center);c.fillCircle(810,920,70,theme::InkPrimary);c.fillCircle(840,894,62,theme::Paper);text::centeredLine(c,{Face::Text,54},{180,1060,1440,1180},"Sleeping",theme::InkPrimary,Align::Center);}
}
