// SPDX-License-Identifier: AGPL-3.0-only
#include "notebook_pdf.h"
#include <algorithm>
#include <cstdio>
#include <vector>
namespace notebook {
std::string pdf(Paper paper,int pages){
 pages=std::clamp(pages,1,64);constexpr int W=540,H=720;
 std::string content="0.78 G 0.78 g 0.6 w\n";char line[96];
 if(paper==Paper::Lined)for(int y=H-90;y>=48;y-=26){std::snprintf(line,sizeof line,"36 %d m %d %d l S\n",y,W-36,y);content+=line;}
 if(paper==Paper::Grid){for(int y=H-18;y>0;y-=18){std::snprintf(line,sizeof line,"0 %d m %d %d l S\n",y,W,y);content+=line;}for(int x=18;x<W;x+=18){std::snprintf(line,sizeof line,"%d 0 m %d %d l S\n",x,x,H);content+=line;}}
 if(paper==Paper::Dots)for(int y=H-18;y>0;y-=18)for(int x=18;x<W;x+=18){std::snprintf(line,sizeof line,"%d %d 0.9 0.9 re f\n",x,y);content+=line;}
 // Objects: 1 catalog, 2 pages, 3 shared content stream, then one page object each.
 std::vector<std::string> objects;
 std::string kids;for(int i=0;i<pages;++i){kids+=std::to_string(4+i)+" 0 R ";}
 objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
 objects.push_back("<< /Type /Pages /Kids [ "+kids+"] /Count "+std::to_string(pages)+" >>");
 objects.push_back("<< /Length "+std::to_string(content.size())+" >>\nstream\n"+content+"endstream");
 for(int i=0;i<pages;++i)objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "+std::to_string(W)+" "+std::to_string(H)+"] /Resources << >> /Contents 3 0 R >>");
 std::string out="%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";std::vector<std::size_t> offsets;
 for(std::size_t i=0;i<objects.size();++i){offsets.push_back(out.size());out+=std::to_string(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n";}
 std::size_t xref=out.size();out+="xref\n0 "+std::to_string(objects.size()+1)+"\n0000000000 65535 f \n";
 for(auto o:offsets){std::snprintf(line,sizeof line,"%010zu 00000 n \n",o);out+=line;}
 out+="trailer\n<< /Size "+std::to_string(objects.size()+1)+" /Root 1 0 R >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
 return out;
}
}
