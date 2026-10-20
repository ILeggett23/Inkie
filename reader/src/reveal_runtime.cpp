#include "ux_shell.h"
#include <limits>
namespace ux {namespace {
std::uint64_t milliseconds()noexcept{auto value=spike::monotonic();return value?value/1000000:std::numeric_limits<std::uint64_t>::max();}
bool covers(spike::Rect a,localized::Rect b)noexcept{return !b.empty()&&a.x0<=b.x0&&a.y0<=b.y0&&a.x1>=b.x1&&a.y1>=b.y1;}
// Presentation scenes only: worker revisions and saved/streamed content never
// start another effect. Each explicitly opened scene receives one attempt.
constexpr int ToolsScene=-10,ViewScene=-11,NotesScene=-12,TomScene=-13,LibraryManageScene=-14,LibraryMenuScene=-15,CompanionScene=-16;
spike::Rect insetContent(spike::Rect panel)noexcept{return {panel.x0+16,panel.y0+24,panel.x1-16,panel.y1-16};}
bool blankStrip(std::span<const std::uint32_t> pixels,spike::Rect content,bool transparent=false)noexcept{
 const spike::Rect band{content.x0+8,content.y0-10,std::min(content.x1-8,content.x0+116),content.y0-2};
 if(band.empty()||band.x0<0||band.y0<0||band.x1>1620||band.y1>2160||pixels.size()<underlay::Pixels)return false;
 for(int y=band.y0;y<band.y1;++y)for(int x=band.x0;x<band.x1;++x){auto pixel=pixels[std::size_t(y)*1632+x];if(pixel!=0xffffffffu&&(!transparent||pixel!=0u))return false;}
 return true;
}
}
product::AnimationPolicy Shell::revealPolicy(bool contact)const noexcept{product::AnimationPolicy p;p.enabled=!stop_&&quietEffectsEnabled();p.reducedMotion=reducedMotion_;p.constrainedPower=powerConstrained_;p.contact=contact||(document_&&document_->contact())||(tom_&&tom_->contact())||(notes_&&notes_->contact());if(p.contact)return p;p.heavyRendering=(state_!=State::Clean&&state_!=State::Chrome&&state_!=State::Picker)||changingView_||syncing_||returning_||companionBusy_||(styled_&&styled_->status().busy)||semantics_->pending()||(notes_&&notes_->active()&&!notes_->revealReady())||(tom_&&tom_->active()&&!tom_->revealReady())||(toolsView_.active()&&toolsView_.pending())||(viewControls_.active()&&viewControls_.pending());return p;}
void Shell::revealSceneChanged(){
 sandSceneChanged();if(prefs_.sandMode||sand_.pending())return;
 if(stop_)return;
 int scene=int(semantics_->view());QString key;spike::Rect content;std::span<const std::uint32_t> panelPixels;bool panelReady=true;
 if(tom_&&tom_->active()){scene=TomScene;content=tom_->revealContent();panelPixels=tom_->background();key=QString::number(tom_->revealSceneEpoch());panelReady=tom_->revealReady();}
 else if(notes_&&notes_->active()){scene=NotesScene;content={0,15,1620,2160};panelPixels=notes_->background();key=QString::number(notes_->revealSceneEpoch());panelReady=notes_->revealReady();}
 else if(toolsView_.active()){scene=ToolsScene;content=insetContent(ToolsView::panel());panelPixels=toolsPixels_;}
 else if(viewControls_.active()){scene=ViewScene;content=insetContent(DocumentControls::Panel);panelPixels=viewPixels_;}
 else if(state_==State::Picker&&companionView_.active()){scene=CompanionScene;content={0,15,1620,2160};panelPixels=background();}
 else if(state_==State::Picker&&libraryManage_.active()){scene=LibraryManageScene;key=QString::number(libraryManage_.effectScene());content={0,15,1620,2160};panelPixels=background();}
 else if(state_==State::Picker&&libraryView_.effectScene()){scene=LibraryMenuScene;key=QString::number(libraryView_.effectScene());content=libraryView_.effectContent();panelPixels=background();}
 else if(semantics_->view()==semantic::View::Definition)content=semanticVisual_.panel;
 else if(semantics_->view()==semantic::View::Menu){content=semanticVisual_.menu;panelPixels=background();}
 if(!quietEffectsEnabled()&&reveal_.quiescent()){revealView_=scene;revealSelection_=key;return;}
 if(scene>=0){auto selection=semantics_->selection();key=selection?selection->id():QString{};}
 if(scene==revealView_&&key==revealSelection_)return;
 // Initial asynchronous binding is part of opening a panel, not an invitation
 // to animate later saves. Record the scene now, invalidating prior adornment.
 // Promote its pending key once, only after its static content is usable.
 const bool panelScene=scene==NotesScene||scene==TomScene;
 const QString pendingKey=panelScene?key+QLatin1String(":opening"):QString{};
 if(panelScene&&!panelReady){if(scene!=revealView_||revealSelection_!=pendingKey){revealView_=scene;revealSelection_=pendingKey;revealOpeningEpoch_=++revealUiEpoch_;}return;}
 const bool interruptedOpening=panelScene&&scene==revealView_&&revealSelection_==pendingKey&&revealOpeningEpoch_!=revealUiEpoch_;
 revealView_=scene;revealSelection_=key;++revealUiEpoch_;
 // Consume this opening even if contact, power, heavy work or user preference
 // suppresses it. Readable content is always the current static scene.
 if(interruptedOpening||!quietEffectsEnabled()||content.empty()||(!panelPixels.empty()&&!blankStrip(panelPixels,content)))return;
 if(semantics_->view()==semantic::View::Menu&&!panelScene&&!blankStrip(visibleInk(),content,true))return;
 auto policy=revealPolicy();const unsigned area=unsigned(std::min(content.x1-content.x0-16,108)*8);if(!policy.permits(area))return;
 (void)reveal_.start({{content.x0,content.y0,content.x1,content.y1},{0,0,1620,2160},{presentedVersion_,revealUiEpoch_},true},policy,milliseconds());revealTick();
}
void Shell::revealTick(){sandTick();if(reveal_.quiescent())return;auto update=reveal_.advance({presentedVersion_,revealUiEpoch_},revealPolicy(),milliseconds());revealUpdate_=update;if(!update)return;if(change_.full)return;if(change_.extraCount<change_.extra.size())damage({update.damage.x0,update.damage.y0,update.damage.x1,update.damage.y1});}
void Shell::cancelRevealForContact()noexcept{cancelSandForInput();++revealUiEpoch_;revealUpdate_=reveal_.advance({presentedVersion_,revealUiEpoch_},revealPolicy(true),milliseconds());revealPaintStamp_=0;}
void Shell::compositeReveal(spike::Surface out,spike::Rect region)const noexcept{revealPaintStamp_=0;if(!revealUpdate_||!covers(region,revealUpdate_.damage))return;if(revealUpdate_.paint){if(reveal_.identity()!=localized::Identity{presentedVersion_,revealUiEpoch_}||!reveal_.paint(revealUpdate_.stamp,out.pixels,out.width,out.height,out.stridePixels))return;}revealPaintStamp_=revealUpdate_.stamp;}
void Shell::revealCovered(spike::Rect region)noexcept{if(sandPaintStamp_)sand_.acknowledge(sandPaintStamp_,{region.x0,region.y0,region.x1,region.y1});if(revealPaintStamp_&&reveal_.acknowledgePresented(revealPaintStamp_,{region.x0,region.y0,region.x1,region.y1})&&reveal_.quiescent()){revealUpdate_={};revealPaintStamp_=0;}}
}
