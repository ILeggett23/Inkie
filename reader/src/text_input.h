#pragma once
#include "ux_paint.h"
#include "ib/tom_screen.h"
#include <QString>
namespace ux {
// Process-wide Inkbound clipboard (Copy in the selection toolbar, Paste on the keyboard).
QString& sharedClipboard();
// Original on-screen keyboard, used only at intercontact UI boundaries.
class TextInput final {
 QString value_,title_;unsigned limit_{256};bool active_{},done_{},cancelled_{},upper_{},symbols_{},secret_{};ib::Targets targets_;
public:
 static constexpr spike::Rect Panel=ib::KeyboardRect;
 // secret: the field shows one dot per character (passwords) and is never pasted from.
 void open(QString value,QString title="Enter text",unsigned limit=256,bool secret=false);
 void paint(spike::Surface);
 bool finger(int x,int y);
 void cancel()noexcept{active_=false;cancelled_=true;done_=false;}
 bool active()const noexcept{return active_;}bool done()const noexcept{return done_;}bool cancelled()const noexcept{return cancelled_;}
 QString text()const{return value_;}
 const ib::Targets& targets()const noexcept{return targets_;}
 void acknowledge()noexcept{done_=cancelled_=false;}
};
}
