// SPDX-License-Identifier: AGPL-3.0-only
// Layout-change fallback: when a reading word cannot be found exactly in a new layout (hyphenation and line breaks
// differ between font sizes), the reader keeps the same share of the same chapter. Every page of one layout maps to
// a page of the same chapter in another, and the share moves by at most one page of that chapter.
#include "epub_reflow.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace {const QString Id="12345678-1234-1234-1234-123456789abc";unsigned checks{};}
static void check(bool b,const char* what){if(!b)throw std::runtime_error(what);++checks;}
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=2)return 64;
 try{
  const auto fixture=std::filesystem::path(argv[1])/"fixtures/library/authored-three-chapters.epub";QFile f(QString::fromStdString(fixture.string()));check(f.open(QIODevice::ReadOnly),"fixture");
  const auto sha=QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex());
  epub::ReflowDocument small(fixture,Id,sha,{600,800,12}),large(fixture,Id,sha,{600,800,18});check(large.pageCount()>=small.pageCount(),"larger type has at least as many pages");
  for(int page=0;page<small.pageCount();++page){auto from=small.chapterFraction(page);check(from.fraction>=0&&from.fraction<=1,"fraction bounded");
   const int target=large.pageAtFraction(from.chapter,from.fraction);check(target>=0&&target<large.pageCount(),"target in range");auto to=large.chapterFraction(target);check(to.chapter==from.chapter,"same chapter");check(to.fraction<=from.fraction+1e-9,"target page starts at or before the share");
   if(target+1<large.pageCount()){auto next=large.chapterFraction(target+1);check(next.chapter!=from.chapter||next.fraction>from.fraction-1e-9,"the next page starts after the share");}}
  for(int chapter=0;chapter<3;++chapter){auto first=large.pageAtFraction(chapter,0),last=large.pageAtFraction(chapter,1);check(large.chapterFraction(first).chapter==chapter&&large.chapterFraction(first).fraction==0,"share 0 is the chapter's first page");check(large.chapterFraction(last).chapter==chapter&&last>=first,"share 1 is in the chapter");
   check(large.pageAtFraction(chapter,-3)==first&&large.pageAtFraction(chapter,7)==last,"shares are clamped");}
  bool refused=false;try{(void)large.pageAtFraction(99,0.5);}catch(const std::exception&){refused=true;}check(refused,"unknown chapter refused");
  std::printf("EPUB_READING_FRACTION_PASS checks=%u small_pages=%d large_pages=%d same_chapter=true monotone=true\n",checks,small.pageCount(),large.pageCount());return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"EPUB_READING_FRACTION_FAILED %s checks=%u\n",e.what(),checks);return 1;}
}
