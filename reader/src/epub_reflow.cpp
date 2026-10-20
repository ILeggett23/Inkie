#include "epub_typography.h"
#include "epub_reflow.h"
#include "turn_fit.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QXmlStreamReader>
#include <QHash>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include <mupdf/fitz.h>
}
namespace epub {namespace {
constexpr qsizetype MaxSource=64*1024*1024,MaxEntry=16*1024*1024,MaxChapterText=1024*1024;
constexpr int MaxChapters=512,MaxPages=1000,MaxChapterPages=256,MaxGlyphs=131072,MaxPageGlyphs=8192,MaxQuote=16000;
void need(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
QString hash(const QByteArray& b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
bool sha(const QString& s){return QRegularExpression("^[0-9a-f]{64}\\z").match(s).hasMatch();}
QJsonObject unresolved(const char* reason){return {{"status","unresolved"},{"reason",reason}};}
// Every lambda below calls only MuPDF C operations and returns trivial data.
// C++ allocations/ownership and throwing validation occur outside fz_try.
template<class F>auto mupdf(fz_context* ctx,F operation){using T=decltype(operation());static_assert(std::is_trivially_copyable_v<T>);T result{};bool failed=false;fz_var(result);fz_var(failed);fz_try(ctx){result=operation();}fz_catch(ctx){failed=true;}if(failed)throw std::runtime_error("epub-reflow-parser");return result;}
struct FD{int n{-1};~FD(){if(n>=0)::close(n);}};
QByteArray snapshot(const std::filesystem::path& path){FD fd{::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};need(fd.n>=0,"epub-reflow-source-open");struct stat st{};need(!fstat(fd.n,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<=MaxSource,"epub-reflow-source-size");QByteArray bytes(st.st_size,Qt::Uninitialized);qsizetype at=0;while(at<bytes.size()){auto n=::read(fd.n,bytes.data()+at,bytes.size()-at);if(n<0&&errno==EINTR)continue;need(n>0,"epub-reflow-source-read");at+=n;}char extra;need(::read(fd.n,&extra,1)==0,"epub-reflow-source-changed");return bytes;}
QString normalizedPath(const QString& base,const QString& href){need(!href.isEmpty()&&href.size()<2048&&!href.startsWith('/')&&!href.contains('\\')&&!href.contains(':')&&!href.contains('#')&&!href.contains('?')&&!href.contains(QChar(0)),"epub-reflow-spine-path");auto bytes=(base.isEmpty()?href:base+'/'+href).toUtf8();need(bytes.size()<2048&&!bytes.toLower().contains("%00"),"epub-reflow-spine-path");fz_urldecode(bytes.data());fz_cleanname(bytes.data());QString result=QString::fromUtf8(bytes.constData());need(!result.isEmpty()&&result!="."&&result!=".."&&!result.startsWith("../")&&!result.startsWith('/')&&!result.contains(':')&&!result.contains('\\')&&!result.contains('#')&&!result.contains('?'),"epub-reflow-spine-path");return result;}
void xmlToken(QXmlStreamReader& xml){need(!xml.isDTD()&&!xml.isEntityReference()&&!xml.isProcessingInstruction(),"epub-reflow-xml-entity");if(xml.isStartElement())need(xml.name().size()<=64,"epub-reflow-xml-name");}
struct Glyph{int offset{},length{};uint code{};std::array<float,8> quad{};int line{},block{};};
struct Page{int global{},chapterPage{},base{};QString text;QVector<Glyph> glyphs;QJsonArray lines,blocks;};
struct Normalized{QString text;QVector<int> from,to;};
Normalized collapse(const QString& raw){Normalized n;for(int i=0;i<raw.size();){int start=i;if(raw[i].isSpace()){while(i<raw.size()&&raw[i].isSpace())++i;if(!n.text.isEmpty()&&i<raw.size()){n.text+=' ';n.from.append(start);n.to.append(i);}}else{n.text+=raw[i];n.from.append(i);n.to.append(i+1);++i;}}return n;}
QString context(const QString& text,int start,int length){int end=std::min(int(text.size()),start+length);if(start<text.size()&&text[start].isLowSurrogate())++start;if(end<text.size()&&end>start&&text[end].isLowSurrogate())--end;return text.mid(start,end-start);}
QJsonArray quad(const Glyph& g){QJsonArray a;for(float v:g.quad)a.append(double(v));return a;}
struct Chapter{int number{};QString raw;Normalized normalized;QVector<Page> pages;};
struct Spine{QString path,sourceHash;};
int uniqueMatch(const QString& text,const QString& quote,const QString& prefix,const QString& suffix){if(quote.isEmpty())return -1;int from=0,found=-1;while((from=text.indexOf(quote,from))>=0){bool before=prefix.isEmpty()||(from>=prefix.size()&&text.mid(from-prefix.size(),prefix.size())==prefix);bool after=suffix.isEmpty()||text.mid(from+quote.size(),suffix.size())==suffix;if(before&&after){if(found>=0)return -2;found=from;}++from;}return found;}
}
struct ReflowDocument::Impl {
 QByteArray bytes;QString document,source,layoutIdentity;Layout layout;QJsonObject layoutObject;
 fz_context* ctx{};fz_archive* archive{};fz_stream* stream{};fz_document* doc{};int pages{};QVector<Spine> spine;std::unique_ptr<Chapter> cached;
 ~Impl(){if(ctx){fz_drop_document(ctx,doc);fz_drop_stream(ctx,stream);fz_drop_archive(ctx,archive);fz_drop_context(ctx);}}
 QByteArray entry(const QString& name){auto encoded=name.toUtf8();const char* path=encoded.constData();need(mupdf(ctx,[&]{return fz_has_archive_entry(ctx,archive,path);}),"epub-reflow-entry-missing");fz_stream* input=mupdf(ctx,[&]{return fz_open_archive_entry(ctx,archive,path);});QByteArray out;std::array<unsigned char,8192> buffer{};try{for(;;){auto n=mupdf(ctx,[&]{return fz_read(ctx,input,buffer.data(),buffer.size());});if(!n)break;need(n<=std::size_t(MaxEntry-out.size()),"epub-reflow-entry-limit");out.append(reinterpret_cast<const char*>(buffer.data()),qsizetype(n));}}catch(...){fz_drop_stream(ctx,input);throw;}fz_drop_stream(ctx,input);return out;}
 void open(const std::filesystem::path& path,QString id,QString expected,Layout requested){
  need(sha(expected)&&(sha(id)||QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\z").match(id).hasMatch()),"epub-reflow-identity");
  need(requested.width>=64&&requested.width<=4094&&requested.height>=64&&requested.height<=4094&&requested.fontSize>=6&&requested.fontSize<=72&&epub::valid(requested.userCss),"epub-reflow-layout");
  bytes=snapshot(path);source=hash(bytes);need(source==expected,"epub-reflow-source-sha-mismatch");document=std::move(id);layout=requested;
  layoutObject={{"engine","MuPDF"},{"version",FZ_VERSION},{"width",layout.width},{"height",layout.height},{"fontSize",layout.fontSize},{"publisherCss",true},{"userCss",layout.userCss},{"textPolicy","collapse-whitespace-v1"}};
  layoutIdentity="epub-reflow-v1:"+hash(QJsonDocument(layoutObject).toJson(QJsonDocument::Compact));
  ctx=fz_new_context(nullptr,nullptr,32*1024*1024);need(ctx,"epub-reflow-context");fz_set_error_callback(ctx,[](void*,const char*){},nullptr);fz_set_warning_callback(ctx,[](void*,const char*){},nullptr);
  const auto* data=reinterpret_cast<const unsigned char*>(bytes.constData());auto length=std::size_t(bytes.size());archive=mupdf(ctx,[&]{return fz_open_zip_archive_with_memory(ctx,data,length);});int entries=mupdf(ctx,[&]{return fz_count_archive_entries(ctx,archive);});need(entries>0&&entries<=4096,"epub-reflow-archive-limit");need(entry("mimetype")=="application/epub+zip","epub-reflow-mimetype");
  QString package;QXmlStreamReader container(entry("META-INF/container.xml"));while(!container.atEnd()){container.readNext();xmlToken(container);if(container.isStartElement()&&container.name()=="rootfile"){need(package.isEmpty(),"epub-reflow-multiple-rootfiles");package=normalizedPath({},container.attributes().value("full-path").toString());}}need(!container.hasError()&&!package.isEmpty(),"epub-reflow-container");
  QString base=package.contains('/')?package.left(package.lastIndexOf('/')):QString{};struct Item{QString href,type;};QHash<QString,Item> items;QStringList refs,stack;
  QXmlStreamReader opf(entry(package));while(!opf.atEnd()){opf.readNext();xmlToken(opf);if(opf.isStartElement()){QString parent=stack.isEmpty()?QString{}:stack.last();auto name=opf.name().toString();if(parent=="manifest"&&name=="item"){auto a=opf.attributes();QString key=a.value("id").toString();need(!key.isEmpty()&&key.size()<=256&&!items.contains(key)&&items.size()<4096,"epub-reflow-manifest");items.insert(key,{a.value("href").toString(),a.value("media-type").toString()});}if(parent=="spine"&&name=="itemref"){need(refs.size()<MaxChapters,"epub-reflow-spine-limit");refs.append(opf.attributes().value("idref").toString());}stack.append(name);need(stack.size()<=64,"epub-reflow-xml-depth");}else if(opf.isEndElement()){need(!stack.isEmpty(),"epub-reflow-xml-depth");stack.removeLast();}}
  need(!opf.hasError()&&!refs.isEmpty(),"epub-reflow-opf");QSet<QString> names;qint64 expanded=0;for(const auto& ref:refs){need(items.contains(ref),"epub-reflow-spine-reference");auto item=items[ref];need(item.type=="application/xhtml+xml"||item.type=="text/html","epub-reflow-spine-type");auto name=normalizedPath(base,item.href);need(!names.contains(name.toCaseFolded()),"epub-reflow-ambiguous-spine-path");names.insert(name.toCaseFolded());auto raw=entry(name);expanded+=raw.size();need(expanded<=256*1024*1024,"epub-reflow-spine-expansion-limit");spine.append({name,hash(raw)});}
  mupdf(ctx,[&]{fz_register_document_handlers(ctx);return 0;});stream=mupdf(ctx,[&]{return fz_open_memory(ctx,data,length);});doc=mupdf(ctx,[&]{return fz_open_document_with_stream(ctx,"application/epub+zip",stream);});char format[32]{};mupdf(ctx,[&]{return fz_lookup_metadata(ctx,doc,FZ_META_FORMAT,format,sizeof format);});need(!std::strcmp(format,"EPUB"),"epub-reflow-format");
  mupdf(ctx,[&]{{const QByteArray userCss=layout.userCss.toUtf8();fz_style_document(ctx,doc,1,userCss.constData());}fz_layout_document(ctx,doc,float(layout.width),float(layout.height),float(layout.fontSize));return 0;});need(mupdf(ctx,[&]{return fz_count_chapters(ctx,doc);})==spine.size(),"epub-reflow-chapter-map");
  for(int chapter=0;chapter<spine.size();++chapter){auto name=spine[chapter].path.toUtf8();const char* uri=name.constData();auto destination=mupdf(ctx,[&]{return fz_resolve_link_dest(ctx,doc,uri);});need(destination.loc.chapter==chapter&&destination.loc.page==0,"epub-reflow-spine-map");}
  pages=mupdf(ctx,[&]{return fz_count_pages(ctx,doc);});need(pages>0&&pages<=MaxPages,"epub-reflow-page-limit");
 }
 Page extract(int chapter,int chapterPage){
  Page out;out.chapterPage=chapterPage;out.global=mupdf(ctx,[&]{return fz_page_number_from_location(ctx,doc,fz_make_location(chapter,chapterPage));});need(out.global>=0&&out.global<pages,"epub-reflow-page-map");
  fz_page* page=mupdf(ctx,[&]{return fz_load_chapter_page(ctx,doc,chapter,chapterPage);});fz_stext_page* text{};try{text=mupdf(ctx,[&]{return fz_new_stext_page_from_page(ctx,page,nullptr);});}catch(...){fz_drop_page(ctx,page);throw;}fz_drop_page(ctx,page);
  try{for(auto* block=text->first_block;block;block=block->next){if(block->type!=FZ_STEXT_BLOCK_TEXT)continue;int blockStart=out.text.size(),blockIndex=out.blocks.size();for(auto* line=block->u.t.first_line;line;line=line->next){int lineStart=out.text.size(),lineIndex=out.lines.size();for(auto* ch=line->first_char;ch;ch=ch->next){need(out.glyphs.size()<MaxPageGlyphs&&ch->c>=0&&ch->c<=0x10ffff&&!(ch->c>=0xd800&&ch->c<=0xdfff)&&!(ch->flags&(FZ_STEXT_UNICODE_IS_CID|FZ_STEXT_UNICODE_IS_GID)),"epub-reflow-text-mapping-limit");char32_t code=char32_t(ch->c);QString glyph=QString::fromUcs4(&code,1);auto q=ch->quad;Glyph record{int(out.text.size()),int(glyph.size()),uint(code),{q.ul.x,q.ul.y,q.ur.x,q.ur.y,q.ll.x,q.ll.y,q.lr.x,q.lr.y},lineIndex,blockIndex};for(float v:record.quad)need(std::isfinite(v)&&std::abs(v)<100000,"epub-reflow-quad");out.glyphs.append(record);out.text+=glyph;}out.lines.append(QJsonArray{lineStart,int(out.text.size()),blockIndex});out.text+='\n';need(out.text.size()<=32768,"epub-reflow-page-text-limit");}out.blocks.append(QJsonArray{blockStart,int(out.text.size())});out.text+='\n';}}catch(...){fz_drop_stext_page(ctx,text);throw;}fz_drop_stext_page(ctx,text);return out;
 }
 Chapter& chapter(int number){need(number>=0&&number<spine.size(),"epub-reflow-chapter");if(cached&&cached->number==number)return *cached;auto built=std::make_unique<Chapter>();built->number=number;int count=mupdf(ctx,[&]{return fz_count_chapter_pages(ctx,doc,number);});need(count>0&&count<=MaxChapterPages,"epub-reflow-chapter-page-limit");int glyphs=0;for(int n=0;n<count;++n){auto p=extract(number,n);p.base=built->raw.size();glyphs+=p.glyphs.size();need(glyphs<=MaxGlyphs&&built->raw.size()+p.text.size()+1<=MaxChapterText,"epub-reflow-chapter-text-limit");built->raw+=p.text;built->raw+='\n';built->pages.append(std::move(p));}built->normalized=collapse(built->raw);cached=std::move(built);return *cached;}
 Page& getPage(int global,Chapter*& owner){need(global>=0&&global<pages,"epub-reflow-page-index");auto location=mupdf(ctx,[&]{return fz_location_from_page_number(ctx,doc,global);});owner=&chapter(location.chapter);need(location.page>=0&&location.page<owner->pages.size()&&owner->pages[location.page].global==global,"epub-reflow-page-map");return owner->pages[location.page];}
 QJsonObject capture(int global,int first,int last,const QString& expected,const QString& kind){
  Chapter* ch{};auto& p=getPage(global,ch);if(first<0||last<=first||last>p.text.size()||last-first>MaxQuote||p.text.mid(first,last-first)!=expected)return unresolved("epub-selection-range-mismatch");
  bool begins=false,ends=false;int glyphs=0;for(const auto& g:p.glyphs){begins|=g.offset==first;ends|=g.offset+g.length==last;if(g.offset>=first&&g.offset+g.length<=last)++glyphs;}if(!begins||!ends||glyphs>4096)return unresolved("epub-selection-glyph-boundary");auto wanted=expected.simplified();if(wanted.isEmpty()||wanted.size()>MaxQuote)return unresolved("epub-selection-no-text");
  int from=p.base+first,to=p.base+last,begin=-1,end=-1;for(int n=0;n<ch->normalized.from.size();++n){if(ch->normalized.from[n]>=from&&ch->normalized.to[n]<=to){if(begin<0)begin=n;end=n+1;}}
  if(begin<0||end<=begin||ch->normalized.text.mid(begin,end-begin)!=wanted)return unresolved("epub-selection-whitespace-boundary");const auto& all=ch->normalized.text;QString prefix=context(all,std::max(0,begin-64),std::min(begin,64)),suffix=context(all,end,64);if(uniqueMatch(all,wanted,prefix,suffix)!=begin)return unresolved("epub-selection-ambiguous");
  QJsonObject anchor{{"version",1},{"kind",kind},{"document",document},{"sourceSha256",source},{"chapter",ch->number},{"spineItem",spine[ch->number].path},{"chapterSourceSha256",spine[ch->number].sourceHash},{"chapterTextSha256",hash(all.toUtf8())},{"exactQuote",expected},{"matchQuote",wanted},{"prefix",prefix},{"suffix",suffix},{"textPolicy","collapse-whitespace-v1"},{"engine",FZ_VERSION},{"origin",QJsonObject{{"layout",layoutObject},{"layoutId",layoutIdentity},{"page",global},{"chapterPage",p.chapterPage},{"first",first},{"last",last}}}};
  return {{"status","anchored"},{"anchor",anchor}};
 }
};
ReflowDocument::ReflowDocument(const std::filesystem::path& path,QString document,QString expected,Layout layout):impl_(std::make_unique<Impl>()){impl_->open(path,std::move(document),std::move(expected),layout);}
ReflowDocument::~ReflowDocument()=default;int ReflowDocument::pageCount()const{return impl_->pages;}QString ReflowDocument::sourceSha256()const{return impl_->source;}QString ReflowDocument::layoutId()const{return impl_->layoutIdentity;}
QJsonObject ReflowDocument::layoutDescriptor()const{return impl_->layoutObject;}
PageRaster ReflowDocument::renderPage(int global){return renderPage(global,0);}
PageRaster ReflowDocument::renderPage(int global,unsigned orientation){
 need(global>=0&&global<impl_->pages&&(orientation==0||orientation==90||orientation==270),"epub-reflow-page-index");auto* ctx=impl_->ctx;auto location=mupdf(ctx,[&]{return fz_location_from_page_number(ctx,impl_->doc,global);});
 fz_page* page=mupdf(ctx,[&]{return fz_load_chapter_page(ctx,impl_->doc,location.chapter,location.page);});fz_pixmap* pixels{};fz_device* device{};
 try{
  auto box=mupdf(ctx,[&]{return fz_bound_page(ctx,page);});double x=box.x0,y=box.y0,w=double(box.x1)-x,h=double(box.y1)-y;need(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(w)&&std::isfinite(h)&&w>0&&h>0,"epub-raster-bounds");
  const auto port=reader::pdfview::viewport(orientation);const int width=orientation?port.width:1620,height=orientation?port.height:2060;const std::size_t maxBytes=std::size_t(1620)*2060*4;double fit=orientation?std::min(port.width/h,port.height/w):std::min(width/w,height/h);need(std::isfinite(fit)&&fit>=0.1&&fit<=16,"epub-raster-fit-scale");
  // MuPDF consumes floats: publish those exact coefficients, not a rounded
  // double calculation which could diverge from the pixels at glyph edges.
  fz_matrix transform{float(fit),0,0,float(fit),float((width-w*fit)/2-x*fit),float((height-h*fit)/2-y*fit)};PageRaster out;out.bounds={x,y,w,h};out.width=out.stride=width;out.height=height;
  if(orientation){const auto t=reader::pdfview::fitTurned(x,y,w,h,orientation);transform={t[0],t[1],t[2],t[3],t[4]-float(port.x),t[5]-float(port.y)}; /* panel pixels to raster pixels */
   const double a=t[0],b=t[1],c=t[2],d=t[3],e=t[4],f=t[5],det=a*d-b*c;need(std::isfinite(det)&&std::abs(det)>0,"epub-raster-transform");out.forward={a,b,c,d,e,f};out.inverse={d/det,-b/det,-c/det,a/det,(c*f-d*e)/det,(b*e-a*f)/det};}
  else{double scale=transform.a,tx=transform.e,ty=transform.f;out.forward={scale,0,0,scale,tx,ty};out.inverse={1/scale,0,0,1/scale,-tx/scale,-ty/scale};}
  for(float v:{transform.a,transform.b,transform.c,transform.d,transform.e,transform.f})need(std::isfinite(v),"epub-raster-transform");
  need(std::size_t(out.stride)*out.height<=maxBytes/sizeof(std::uint32_t),"epub-raster-output-capacity");out.rgb32.resize(std::size_t(out.stride)*out.height);
  pixels=mupdf(ctx,[&]{return fz_new_pixmap_with_bbox(ctx,fz_device_rgb(ctx),fz_irect{0,0,width,height},nullptr,0);});mupdf(ctx,[&]{fz_clear_pixmap_with_value(ctx,pixels,255);return 0;});device=mupdf(ctx,[&]{return fz_new_draw_device(ctx,fz_identity,pixels);});mupdf(ctx,[&]{fz_run_page(ctx,page,device,transform,nullptr);fz_close_device(ctx,device);return 0;});fz_drop_device(ctx,device);device=nullptr;
  need(pixels->w==width&&pixels->h==height&&pixels->n==3&&pixels->alpha==0&&pixels->stride>=width*3&&std::size_t(pixels->stride)*height<=maxBytes,"epub-raster-pixel-layout");for(int row=0;row<height;++row){const auto* source=pixels->samples+std::size_t(row)*pixels->stride;for(int col=0;col<width;++col){auto i=col*3;out.rgb32[std::size_t(row)*out.stride+col]=0xff000000u|(std::uint32_t(source[i])<<16)|(std::uint32_t(source[i+1])<<8)|source[i+2];}}
  fz_drop_pixmap(ctx,pixels);fz_drop_page(ctx,page);return out;
 }catch(...){fz_drop_device(ctx,device);fz_drop_pixmap(ctx,pixels);fz_drop_page(ctx,page);throw;}
}
QJsonObject ReflowDocument::page(int global){Chapter* chapter{};auto& page=impl_->getPage(global,chapter);QJsonArray glyphs;for(const auto& g:page.glyphs)glyphs.append(QJsonObject{{"offset",g.offset},{"length",g.length},{"text",page.text.mid(g.offset,g.length)},{"u",int(g.code)},{"line",g.line},{"block",g.block},{"quad",quad(g)}});return {{"page",global},{"chapter",chapter->number},{"chapterPage",page.chapterPage},{"spineItem",impl_->spine[chapter->number].path},{"layoutId",impl_->layoutIdentity},{"sourceSha256",impl_->source},{"text",page.text},{"chars",glyphs},{"lines",page.lines},{"blocks",page.blocks}};}
QJsonObject ReflowDocument::captureSelection(int page,int first,int last,const QString& expected){return impl_->capture(page,first,last,expected,"text-selection");}
QJsonObject ReflowDocument::captureReadingPosition(int global){Chapter* chapter{};auto& p=impl_->getPage(global,chapter);const Glyph* best=nullptr;for(const auto& g:p.glyphs){if(!QChar::isLetterOrNumber(g.code))continue;float y=std::min({g.quad[1],g.quad[3],g.quad[5],g.quad[7]}),x=std::min({g.quad[0],g.quad[2],g.quad[4],g.quad[6]});if(y<0||y>=impl_->layout.height||x<0||x>=impl_->layout.width)continue;if(!best||y<std::min(best->quad[1],best->quad[3])-0.01f||(std::abs(y-std::min(best->quad[1],best->quad[3]))<=0.01f&&x<best->quad[0]))best=&g;}if(!best)return unresolved("epub-reading-position-no-text");int start=best->offset;while(start>0&&p.text[start-1].isLetterOrNumber())--start;int end=best->offset+best->length;while(end<p.text.size()&&(p.text[end].isLetterOrNumber()||p.text[end]=='\''||p.text[end]==QChar(0x2019)))++end;if(end-start>128)return unresolved("epub-reading-word-limit");return impl_->capture(global,start,end,p.text.mid(start,end-start),"reading-position");}
ReflowDocument::ChapterFraction ReflowDocument::chapterFraction(int global){Chapter* chapter{};auto& p=impl_->getPage(global,chapter);const double total=std::max<qsizetype>(1,chapter->raw.size());return {chapter->number,std::clamp(double(p.base)/total,0.0,1.0)};}
int ReflowDocument::pageAtFraction(int number,double fraction){auto& ch=impl_->chapter(number);need(!ch.pages.isEmpty(),"epub-reflow-chapter-empty");const double at=std::clamp(fraction,0.0,1.0)*double(ch.raw.size());int global=ch.pages.first().global;for(const auto& p:ch.pages){if(double(p.base)>at)break;global=p.global;}return global;}
QJsonObject ReflowDocument::captureSpineStart(int global){Chapter* chapter{};auto& p=impl_->getPage(global,chapter);if(global!=0||chapter->number!=0||p.chapterPage!=0||!p.text.trimmed().isEmpty())return unresolved("epub-spine-start-not-textless-cover");return {{"status","anchored"},{"anchor",QJsonObject{{"version",1},{"kind","spine-start"},{"document",impl_->document},{"sourceSha256",impl_->source},{"chapter",0},{"spineItem",impl_->spine[0].path},{"chapterSourceSha256",impl_->spine[0].sourceHash},{"engine",FZ_VERSION}}}};}
QJsonObject ReflowDocument::resolve(const QJsonObject& anchor){
 if(anchor["kind"]=="spine-start"){
  static const QSet<QString> fields{"version","kind","document","sourceSha256","chapter","spineItem","chapterSourceSha256","engine"};if(anchor.size()!=fields.size())return unresolved("epub-spine-start-schema");for(auto i=anchor.begin();i!=anchor.end();++i)if(!fields.contains(i.key()))return unresolved("epub-spine-start-schema");
  if(anchor["version"]!=1||anchor["engine"]!=FZ_VERSION||anchor["document"]!=impl_->document||anchor["sourceSha256"]!=impl_->source||anchor["chapter"]!=0||anchor["spineItem"]!=impl_->spine[0].path||anchor["chapterSourceSha256"]!=impl_->spine[0].sourceHash)return unresolved("epub-spine-start-identity");
  int target=mupdf(impl_->ctx,[&]{return fz_page_number_from_location(impl_->ctx,impl_->doc,fz_make_location(0,0));});Chapter* chapter{};auto& p=impl_->getPage(target,chapter);if(chapter->number!=0||p.chapterPage!=0)return unresolved("epub-spine-start-target");return {{"status","resolved"},{"kind","spine-start"},{"document",impl_->document},{"sourceSha256",impl_->source},{"chapter",0},{"spineItem",impl_->spine[0].path},{"layoutId",impl_->layoutIdentity},{"page",target},{"fragments",QJsonArray{}}};
 }

 static const QSet<QString> fields{"version","kind","document","sourceSha256","chapter","spineItem","chapterSourceSha256","chapterTextSha256","exactQuote","matchQuote","prefix","suffix","textPolicy","engine","origin"};for(auto i=anchor.begin();i!=anchor.end();++i)if(!fields.contains(i.key()))return unresolved("epub-anchor-fields");
 if(anchor["version"].toInt()!=1||(anchor["kind"]!="reading-position"&&anchor["kind"]!="text-selection")||anchor["textPolicy"]!="collapse-whitespace-v1"||anchor["engine"]!=FZ_VERSION)return unresolved("epub-anchor-schema");
 if(anchor["sourceSha256"]!=impl_->source||anchor["document"]!=impl_->document)return unresolved("epub-anchor-source-identity");
 int chapter=anchor["chapter"].toInt(-1);if(!anchor["chapter"].isDouble()||anchor["chapter"].toDouble()!=chapter||chapter<0||chapter>=impl_->spine.size()||anchor["spineItem"]!=impl_->spine[chapter].path||anchor["chapterSourceSha256"]!=impl_->spine[chapter].sourceHash)return unresolved("epub-anchor-spine-identity");
 QString quote=anchor["matchQuote"].toString(),exact=anchor["exactQuote"].toString(),prefix=anchor["prefix"].toString(),suffix=anchor["suffix"].toString();if(quote.isEmpty()||quote.size()>MaxQuote||exact.size()>MaxQuote||exact.simplified()!=quote||prefix.size()>64||suffix.size()>64||!sha(anchor["chapterTextSha256"].toString()))return unresolved("epub-anchor-text-bounds");
 auto& ch=impl_->chapter(chapter);if(hash(ch.normalized.text.toUtf8())!=anchor["chapterTextSha256"])return unresolved("epub-chapter-text-changed");int match=uniqueMatch(ch.normalized.text,quote,prefix,suffix);if(match<0)return unresolved(match==-2?"epub-anchor-ambiguous":"epub-anchor-quote-missing");int from=ch.normalized.from[match],to=ch.normalized.to[match+quote.size()-1];QJsonArray fragments;unsigned count=0;
 for(const auto& p:ch.pages){QJsonArray quads;int first=-1,last=-1;for(const auto& g:p.glyphs){int start=p.base+g.offset,end=start+g.length;if(start>=from&&end<=to){if(first<0)first=g.offset;last=g.offset+g.length;quads.append(quad(g));if(++count>4096)return unresolved("epub-resolved-glyph-limit");}}if(first>=0)fragments.append(QJsonObject{{"page",p.global},{"chapterPage",p.chapterPage},{"first",first},{"last",last},{"text",p.text.mid(first,last-first)},{"quads",quads}});}
 if(fragments.isEmpty())return unresolved("epub-anchor-no-quads");return {{"status","resolved"},{"kind",anchor["kind"]},{"document",impl_->document},{"sourceSha256",impl_->source},{"chapter",chapter},{"spineItem",impl_->spine[chapter].path},{"layoutId",impl_->layoutIdentity},{"page",fragments.first().toObject()["page"]},{"fragments",fragments}};
}
}
