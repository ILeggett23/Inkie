/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <QString>
#include <vector>
namespace tomink {
class VisualExpression {struct Node {QString op;double value{};int a{-1},b{-1};};QString source_;int at_{},depth_{},root_{};std::vector<Node> nodes_;void space();int node(QString,double=0,int=-1,int=-1);bool eat(QChar);int sum();int term();int unary();int power();int atom();double evaluate(int,double)const;
public:explicit VisualExpression(QString);double operator()(double x)const;
};
}
