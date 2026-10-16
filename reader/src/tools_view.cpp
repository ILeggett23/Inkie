#include "tools_view.h"
#include <array>
namespace ux {namespace {
constexpr std::array<const char*,9> Tools{"BALLPOINT","FINELINER","PENCIL","MECHANICAL","PAINTBRUSH","CALLIGRAPHY","MARKER","SHADER","HIGHLIGHTER"};
constexpr std::array<const char*,8> Colors{"BLACK","PINK","GREEN","ORANGE","YELLOW","PURPLE","BLUE","RED"};
bool inside(spike::Rect r,int x,int y)noexcept{return x>=r.x0&&x<r.x1&&y>=r.y0&&y<r.y1;}
}
spike::Rect ToolsView::toolRect(unsigned n)noexcept{return ib::reader::penCell(n);}
spike::Rect ToolsView::widthRect(unsigned n)noexcept{return ib::reader::widthCell(n);}
spike::Rect ToolsView::colorRect(unsigned n)noexcept{return ib::reader::colorCell(n);}
const char* ToolsView::toolName(Tool t)noexcept{return unsigned(t)<Tools.size()?Tools[unsigned(t)]:"INVALID";}
const char* ToolsView::colorName(unsigned c)noexcept{return c<Colors.size()?Colors[c]:"INVALID";}
std::uint32_t ToolsView::colorRgb(unsigned c)noexcept{return c<ib::theme::Ink.size()?ib::theme::Ink[c]:0xff000000u;}
bool ToolsView::valid(const Snapshot& s)noexcept{return !s.binding.document.empty()&&s.binding.document.size()<=128&&!s.binding.page.empty()&&s.binding.page.size()<=128&&s.binding.revision&&unsigned(s.style.tool)<9&&unsigned(s.style.width)<3&&s.style.color<8&&(s.style.tool!=Tool::Highlighter||s.style.color>0);}
bool ToolsView::open(const Snapshot& s){if(!valid(s))return false;snapshot_=s;highlighterAnchor_=s.shortcut<2?s.shortcut==1:s.style.tool==Tool::Highlighter;active_=true;pending_=false;damage_=panel();return true;}
bool ToolsView::update(const Snapshot& s){if(!active_||!valid(s))return false;if(s.binding.document==snapshot_.binding.document&&s.binding.page==snapshot_.binding.page&&s.binding.revision<snapshot_.binding.revision)return false;if(s.binding!=snapshot_.binding)pending_=false;snapshot_=s;damage_=panel();return true;}
ToolsView::Command ToolsView::hit(int x,int y,const Snapshot& now){
 if(!active_)return {};Command out{Action::None,snapshot_.binding,snapshot_.style,Refusal::None,true};
 if(!valid(now)||now.binding!=snapshot_.binding||now.style!=snapshot_.style||now.quietEffects!=snapshot_.quietEffects||now.snapHighlights!=snapshot_.snapHighlights){out.refusal=Refusal::Stale;return out;}
 if(now.contact){out.refusal=Refusal::Busy;return out;}
 if(inside(closeRect(),x,y)){out.action=Action::Close;active_=pending_=false;damage_=panel();return out;}
 bool style=inside(eraser(),x,y);for(unsigned n=0;n<9;++n)style|=inside(toolRect(n),x,y);for(unsigned n=0;n<3;++n)style|=inside(widthRect(n),x,y);for(unsigned n=0;n<8;++n)style|=inside(colorRect(n),x,y);if(pending_||(now.busy&&!style)){out.refusal=Refusal::Busy;return out;}
 for(unsigned n=0;n<9;++n)if(inside(toolRect(n),x,y)){out.action=Action::Tool;out.style.tool=Tool(n);out.style.eraser=false;if(out.style.tool==Tool::Highlighter&&out.style.color==0)out.style.color=4;}
 for(unsigned n=0;n<3;++n)if(inside(widthRect(n),x,y)){out.action=Action::Width;out.style.width=Width(n);}
 for(unsigned n=0;n<8;++n)if(inside(colorRect(n),x,y)){if(now.style.tool==Tool::Highlighter&&n==0){out.refusal=Refusal::Unavailable;return out;}out.action=Action::Color;out.style.color=n;}
 if(inside(eraser(),x,y)){out.action=Action::Eraser;out.style.eraser=!now.style.eraser;}
 if(inside(undo(),x,y)){if(!now.canUndo){out.refusal=Refusal::Unavailable;return out;}out.action=Action::Undo;}
 if(inside(redo(),x,y)){if(!now.canRedo){out.refusal=Refusal::Unavailable;return out;}out.action=Action::Redo;}
 if(inside(ib::reader::snapCell(),x,y)&&now.style.tool==Tool::Highlighter&&!now.style.eraser){out.action=Action::Snap;out.snapHighlights=!now.snapHighlights;}
 if(out.action!=Action::None){pending_=true;damage_=panel();}return out;
}
bool ToolsView::paint(spike::Surface s)const noexcept{
 if(!active_)return true;
 if(s.width!=1620||s.height!=2160||s.stridePixels<1620||s.pixels.size()<std::size_t(s.stridePixels)*2160)return false;
 ib::Canvas c(s);ib::Targets t;ib::reader::Tools m;auto tool=unsigned(snapshot_.style.tool);
 m.highlighter=snapshot_.style.tool==Tool::Highlighter;m.pen=m.highlighter?ib::Instrument::Fineliner:ib::Instrument(tool);m.eraser=snapshot_.style.eraser;
 m.color=m.highlighter?0:snapshot_.style.color;m.highlightColor=m.highlighter?snapshot_.style.color:4;m.ink=colorRgb(m.color);m.width=ib::reader::Width(unsigned(snapshot_.style.width));
 m.busy=snapshot_.contact||pending_;m.canUndo=snapshot_.canUndo;m.canRedo=snapshot_.canRedo;m.panel=true;m.snapHighlights=snapshot_.snapHighlights;m.canSnap=!snapshot_.busy&&!snapshot_.contact&&!pending_;
 m.status=snapshot_.contact?"Finish the stroke to change tools":m.busy?"Saving\xE2\x80\xA6":"";
 ib::reader::penPanel(c,t,m);return true; // caller pre-fills Panel with the page
}
}
