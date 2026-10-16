/* SPDX-License-Identifier: AGPL-3.0-only */
#include "visual_math.h"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace tomink {namespace {void need(bool b){if(!b)throw std::runtime_error("visual-math-expression");}}
void VisualExpression::space(){while(at_<source_.size()&&source_[at_].isSpace())++at_;}bool VisualExpression::eat(QChar c){space();if(at_<source_.size()&&source_[at_]==c){++at_;return true;}return false;}int VisualExpression::node(QString op,double value,int a,int b){need(nodes_.size()<128);nodes_.push_back({std::move(op),value,a,b});return int(nodes_.size())-1;}
int VisualExpression::sum(){int a=term();for(;;)if(eat('+'))a=node("+",0,a,term());else if(eat('-'))a=node("-",0,a,term());else return a;}
int VisualExpression::term(){int a=unary();for(;;)if(eat('*'))a=node("*",0,a,unary());else if(eat('/'))a=node("/",0,a,unary());else return a;}
int VisualExpression::unary(){need(++depth_<=32);int a;if(eat('+'))a=unary();else if(eat('-'))a=node("neg",0,unary());else a=power();--depth_;return a;}
int VisualExpression::power(){int a=atom();if(eat('^'))a=node("^",0,a,unary());return a;}
int VisualExpression::atom(){space();if(eat('(')){int a=sum();need(eat(')'));return a;}need(at_<source_.size());if(source_[at_].isLetter()){int start=at_;while(at_<source_.size()&&source_[at_].isLetter())++at_;auto name=source_.mid(start,at_-start);if(name=="x")return node("x");if(name=="pi")return node("n",3.141592653589793);if(name=="e")return node("n",2.718281828459045);need(name=="sin"||name=="cos"||name=="tan"||name=="log"||name=="sqrt"||name=="exp"||name=="abs");need(eat('('));int a=sum();need(eat(')'));return node(name,0,a);}int start=at_;bool dot=false;while(at_<source_.size()&&(source_[at_].isDigit()||source_[at_]=='.')){if(source_[at_]=='.'){need(!dot);dot=true;}++at_;}need(at_>start);if(at_<source_.size()&&(source_[at_]=='e'||source_[at_]=='E')){++at_;if(at_<source_.size()&&(source_[at_]=='+'||source_[at_]=='-'))++at_;int digits=at_;while(at_<source_.size()&&source_[at_].isDigit())++at_;need(at_>digits);}bool ok=false;double v=source_.mid(start,at_-start).toDouble(&ok);need(ok&&std::isfinite(v)&&std::abs(v)<=1e12);return node("n",v);}
VisualExpression::VisualExpression(QString s):source_(std::move(s)){need(source_.size()>0&&source_.size()<=256);root_=sum();space();need(at_==source_.size());}
double VisualExpression::evaluate(int i,double x)const{const auto& n=nodes_[std::size_t(i)];if(n.op=="n")return n.value;if(n.op=="x")return x;double a=evaluate(n.a,x);if(n.op=="neg")return -a;if(n.op=="sin")return std::sin(a);if(n.op=="cos")return std::cos(a);if(n.op=="tan")return std::tan(a);if(n.op=="log")return std::log(a);if(n.op=="sqrt")return std::sqrt(a);if(n.op=="exp")return std::exp(a);if(n.op=="abs")return std::abs(a);double b=evaluate(n.b,x);if(n.op=="+")return a+b;if(n.op=="-")return a-b;if(n.op=="*")return a*b;if(n.op=="/")return b==0?std::numeric_limits<double>::quiet_NaN():a/b;return std::pow(a,b);}
double VisualExpression::operator()(double x)const{double y=evaluate(root_,x);return std::isfinite(y)?y:std::numeric_limits<double>::quiet_NaN();}
}
