#include "text_input.h"
#include <algorithm>
namespace ux {
QString& sharedClipboard(){static QString clipboard;return clipboard;}
void TextInput::open(QString value,QString title,unsigned limit,bool secret){limit_=std::clamp(limit,1u,8192u);value_=std::move(value);title_=title.left(56);active_=true;done_=cancelled_=false;upper_=false;symbols_=false;secret_=secret;}
void TextInput::paint(spike::Surface s){if(!active_)return;ib::Canvas c(s);targets_.clear();ib::KeyboardModel m;auto t=title_.toUtf8(),v=secret_?QString(value_.size(),QChar(0x2022)).toUtf8():value_.toUtf8();m.title=std::string(t.constData(),std::size_t(t.size()));m.value=std::string(v.constData(),std::size_t(v.size()));
 m.hint="Type here";m.shift=upper_;m.symbols=symbols_;m.multiline=limit_>256;m.paste=!secret_&&!sharedClipboard().isEmpty();ib::keyboard(c,targets_,m,Panel);}
bool TextInput::finger(int x,int y){if(!active_)return false;int id=targets_.find(x,y);
 if(id>=ib::KeyBase&&id<ib::KeyBase+128){if(value_.size()<int(limit_))value_+=QChar(char16_t(ib::keyCharacter(id,upper_)));upper_=false;return true;}
 switch(id){
 case ib::KeyShift:upper_=!upper_;break;case ib::KeySymbols:symbols_=!symbols_;break;
 case ib::KeySpace:if(value_.size()<int(limit_))value_+=' ';break;case ib::KeyReturn:if(value_.size()<int(limit_))value_+='\n';break;
 case ib::KeyBackspace:if(!value_.isEmpty()){if(value_.back().isLowSurrogate()&&value_.size()>1)value_.chop(2);else value_.chop(1);}break;
 case ib::KeyPaste:if(!secret_)value_+=sharedClipboard().left(std::max(0,int(limit_)-int(value_.size())));break;
 case ib::KeyClear:value_.clear();break;
 case ib::KeyCancel:cancel();break;
 case ib::KeyDone:active_=false;done_=true;cancelled_=false;break;
 default:break;}
 return true;
}
}
