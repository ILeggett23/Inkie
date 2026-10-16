#pragma once
#include "core.h"
#include <cstdio>
namespace reader {
inline std::array<unsigned char,7> glyph(char c){
 switch(c){
 case 'P':return {30,17,17,30,16,16,16};case 'R':return {30,17,17,30,20,18,17};case 'E':return {31,16,16,30,16,16,31};case 'V':return {17,17,17,17,17,10,4};
 case 'N':return {17,25,25,21,19,19,17};case 'X':return {17,17,10,4,10,17,17};case 'T':return {31,4,4,4,4,4,4};case 'I':return {31,4,4,4,4,4,31};
 case 'L':return {16,16,16,16,16,16,31};case 'O':return {14,17,17,17,17,17,14};case 'A':return {14,17,17,31,17,17,17};case 'D':return {30,17,17,17,17,17,30};case 'G':return {14,17,16,23,17,17,14};
 case '0':return {14,17,19,21,25,17,14};case '1':return {4,12,4,4,4,4,14};case '2':return {14,17,1,2,4,8,31};case '3':return {30,1,1,14,1,1,30};case '4':return {2,6,10,18,31,2,2};case '5':return {31,16,16,30,1,1,30};case '6':return {14,16,16,30,17,17,14};case '7':return {31,1,2,4,8,8,8};case '8':return {14,17,17,14,17,17,14};case '9':return {14,17,17,15,1,1,14};case '/':return {1,1,2,4,8,16,16};default:return {};}
}
inline void chrome(spike::Surface s,int page,int total,bool loading=false){
 auto pixel=[&](int x,int y,std::uint32_t c){if(x>=0&&y>=0&&x<s.width&&y<s.height)s.pixels[std::size_t(y)*s.stridePixels+x]=c;};
 for(int y=s.height-100;y<s.height;++y)for(int x=0;x<s.width;++x)pixel(x,y,0xffffffffu);
 auto text=[&](int x,const char* p){for(;*p;++p,x+=30){auto g=glyph(*p);for(int y=0;y<7;++y)for(int b=0;b<5;++b)if(g[y]&(16>>b))for(int yy=0;yy<5;++yy)for(int xx=0;xx<5;++xx)pixel(x+b*5+xx,s.height-67+y*5+yy,0xff000000u);}};
 for(int x=0;x<s.width;++x)pixel(x,s.height-100,0xff000000u);
 text(100,"PREV");text(500,"NEXT");text(1370,"EXIT");char indicator[32];std::snprintf(indicator,sizeof indicator,"%d/%d",page+1,total);text(900,loading?"LOADING":indicator);
}
enum class Action:unsigned {None,Previous,Next,Exit};
inline Action hit(int x,int y){if(y<2060||y>=2160||x<0||x>=1620)return Action::None;if(x<400)return Action::Previous;if(x<800)return Action::Next;if(x>=1300)return Action::Exit;return Action::None;}
}
