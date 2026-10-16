#include "semantic.h"
#include <mupdf/fitz.h>
#include <cstdio>
#include <memory>
#include <sys/stat.h>
namespace semantic {
namespace {
// Page-server helper: the source digest and the opened, laid-out text document are kept between pages while the
// file's identity is unchanged (see underlay::prepare); any MuPDF error drops them.
struct FileIdentity {dev_t dev{};ino_t ino{};off_t size{};long long ms{},mn{},cs{},cn{};bool operator==(const FileIdentity&)const=default;};
bool fileIdentity(const std::filesystem::path& p,FileIdentity& i){struct stat st{};if(::stat(p.c_str(),&st)||!S_ISREG(st.st_mode))return false;i={st.st_dev,st.st_ino,st.st_size,st.st_mtim.tv_sec,st.st_mtim.tv_nsec,st.st_ctim.tv_sec,st.st_ctim.tv_nsec};return true;}
struct TextSource {std::filesystem::path path;FileIdentity id;QString digest;fz_context* ctx{};fz_document* doc{};~TextSource(){if(ctx){fz_drop_document(ctx,doc);fz_drop_context(ctx);}}};
std::unique_ptr<TextSource> textSource; /* the helper is single-threaded */
}
Geometry extract(const std::filesystem::path& file,int index,const QByteArray& binding,const QJsonObject& metadata){
 probe(Work::Text);FileIdentity id{};const bool known=fileIdentity(file,id);if(!(textSource&&known&&textSource->path==file&&textSource->id==id)){textSource.reset();auto fresh=std::make_unique<TextSource>();fresh->path=file;fresh->id=id;fresh->digest=QString::fromLatin1(underlay::digest(underlay::readBounded(file,64*1024*1024)).toHex());FileIdentity after{};if(known&&fileIdentity(file,after)&&after==id)textSource=std::move(fresh);else throw std::runtime_error("text-source-changed");}auto document=textSource->digest;if(metadata.contains("pdf")&&metadata["pdf"].toString()!=document)throw std::runtime_error("text-source-identity");if(metadata.contains("document"))document=metadata["document"].toString();QJsonObject out{{"version",1},{"document",document},{"pageId",QString::fromLatin1(binding.toHex())},{"page",index},{"status","UNAVAILABLE"},{"transform",metadata["pageToSurface"]},{"transformVersion",metadata["render"]}};
 if(metadata["schema"]==3){out["viewId"]=metadata["view_id"];out["visibleSourceRect"]=metadata["crop"];}
 out["transformId"]=QString::fromLatin1(underlay::digest(QJsonDocument(QJsonObject{{"matrix",out["transform"]},{"version",out["transformVersion"]}}).toJson(QJsonDocument::Compact)).toHex());
 auto begin=spike::monotonic();if(!textSource->ctx){textSource->ctx=fz_new_context(nullptr,nullptr,32*1024*1024);if(!textSource->ctx){textSource.reset();return std::make_shared<const TextGeometry>(out);}}
 fz_context* ctx=textSource->ctx;fz_document* doc=textSource->doc;fz_page* page=nullptr;fz_stext_page* text=nullptr;fz_var(doc);fz_var(page);fz_var(text);bool ok=true;fz_var(ok);
 fz_try(ctx){if(!doc){fz_register_document_handlers(ctx);doc=fz_open_document(ctx,file.c_str());if(fz_is_document_reflowable(ctx,doc))fz_layout_document(ctx,doc,600,800,12);textSource->doc=doc;}if(index<0||index>=fz_count_pages(ctx,doc))fz_throw(ctx,FZ_ERROR_ARGUMENT,"page-index");page=fz_load_page(ctx,doc,index);fz_stext_options options{};text=fz_new_stext_page_from_page(ctx,page,&options);}
 fz_always(ctx){fz_drop_page(ctx,page);}
 fz_catch(ctx){if(doc&&doc!=textSource->doc)fz_drop_document(ctx,doc);ok=false;fz_report_error(ctx);}
 try{if(!ok||!text)throw std::runtime_error("text-extract");QJsonArray chars,lines,blocks,words;QString full;
  for(auto* block=text->first_block;block;block=block->next){if(block->type!=FZ_STEXT_BLOCK_TEXT)continue;if(blocks.size()>=MaxBlocks)throw std::runtime_error("text-block-capacity");int blockStart=full.size(),blockIndex=blocks.size();
   for(auto* line=block->u.t.first_line;line;line=line->next){if(lines.size()>=MaxLines)throw std::runtime_error("text-line-capacity");int lineStart=full.size(),lineIndex=lines.size();
    for(auto* ch=line->first_char;ch;ch=ch->next){if(chars.size()>=MaxChars||ch->c<0||ch->c>0x10ffff||(ch->c>=0xd800&&ch->c<=0xdfff)||(ch->flags&(FZ_STEXT_UNICODE_IS_CID|FZ_STEXT_UNICODE_IS_GID)))throw std::runtime_error("text-character-capacity-or-mapping");char32_t value=char32_t(ch->c);QString glyph=QString::fromUcs4(&value,1);auto q=ch->quad;chars.append(QJsonObject{{"text",glyph},{"u",ch->c},{"offset",int(full.size())},{"line",lineIndex},{"block",blockIndex},{"quad",QJsonArray{q.ul.x,q.ul.y,q.ur.x,q.ur.y,q.ll.x,q.ll.y,q.lr.x,q.lr.y}}});full+=glyph;}
    lines.append(QJsonArray{lineStart,int(full.size()),blockIndex});full+='\n';
   }blocks.append(QJsonArray{blockStart,int(full.size())});full+='\n';
  }
  auto lexicalChar=[&](int i){auto c=chars[i].toObject();uint u=uint(c["u"].toInt());if(QChar::isLetterOrNumber(u)||QChar::isMark(u))return true;if((u=='\''||u==0x2019)&&i>0&&i+1<chars.size())return QChar::isLetter(uint(chars[i-1].toObject()["u"].toInt()))&&QChar::isLetter(uint(chars[i+1].toObject()["u"].toInt()));return false;};
  for(int i=0;i<chars.size();){if(!lexicalChar(i)){++i;continue;}int start=i,line=chars[i].toObject()["line"].toInt();while(i<chars.size()&&chars[i].toObject()["line"].toInt()==line&&lexicalChar(i))++i;words.append(QJsonArray{start,i});if(words.size()>MaxWords)throw std::runtime_error("text-word-capacity");}
  out["status"]=chars.isEmpty()?"NO TEXT LAYER":"READY";out["text"]=full;out["chars"]=chars;out["lines"]=lines;out["blocks"]=blocks;out["words"]=words;
 }catch(const std::exception& e){out["status"]="UNAVAILABLE";out["reason"]=e.what();}
 fz_drop_stext_page(ctx,text);if(!ok||textSource->id!=id)textSource.reset(); /* keeps the laid-out document for the next page */auto end=spike::monotonic();std::printf("TEXT_EXTRACT page=%d start_ns=%llu end_ns=%llu status=%s\n",index,(unsigned long long)begin,(unsigned long long)end,out["status"].toString().toUtf8().constData());std::fflush(stdout);return decodeGeometry(out,binding);
}
}
