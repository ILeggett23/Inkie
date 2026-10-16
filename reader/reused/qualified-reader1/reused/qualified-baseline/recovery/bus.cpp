// SPDX-License-Identifier: AGPL-3.0-only
#include "bus.h"
#include <stdexcept>
#include <sstream>
namespace recovery {
static constexpr auto Service="org.freedesktop.systemd1",Path="/org/freedesktop/systemd1",Manager="org.freedesktop.systemd1.Manager";
static void check(int r,const char*what){if(r<0)throw std::runtime_error(std::string(what)+":"+std::to_string(r));}
Bus::Bus(){check(sd_bus_open_system(&bus_),"system bus");check(sd_bus_set_method_call_timeout(bus_,500000),"bus timeout");}
Bus::~Bus(){disconnect();}
void Bus::disconnect(){if(bus_){sd_bus_flush_close_unref(bus_);bus_=nullptr;}}
bool Bus::alive()const{return bus_&&sd_bus_is_open(bus_)>0&&sd_bus_is_ready(bus_)>0;}
std::string Bus::call(const std::string&m,const std::string&unit,bool mode){if(!bus_)throw std::runtime_error("reference connection lost");sd_bus_error error=SD_BUS_ERROR_NULL;sd_bus_message*reply=nullptr;int rc=mode?sd_bus_call_method(bus_,Service,Path,Manager,m.c_str(),&error,&reply,"ss",unit.c_str(),"replace"):sd_bus_call_method(bus_,Service,Path,Manager,m.c_str(),&error,&reply,"s",unit.c_str());std::string msg=error.message?error.message:"";sd_bus_error_free(&error);if(rc<0){sd_bus_message_unref(reply);throw std::runtime_error(m+":"+msg+":"+std::to_string(rc));}const char*value=nullptr;std::string result;if(sd_bus_message_read(reply,"o",&value)>0&&value)result=value;sd_bus_message_unref(reply);return result;}
State Bus::state(const std::string&unit){
 auto path=call("GetUnit",unit);if(path.empty())throw std::runtime_error("unit path");State s;s.boot=boot();
 auto text=[&](const char*iface,const char*prop){char*v=nullptr;int rc=sd_bus_get_property_string(bus_,Service,path.c_str(),iface,prop,nullptr,&v);check(rc,prop);std::string out=v?v:"";free(v);return out;};
 const char*ui="org.freedesktop.systemd1.Unit";const char*si="org.freedesktop.systemd1.Service";
 s.id=text(ui,"Id");s.load=text(ui,"LoadState");s.active=text(ui,"ActiveState");s.sub=text(ui,"SubState");s.group=text(si,"ControlGroup");
 auto number=[&](const char*prop){std::uint32_t n{};check(sd_bus_get_property_trivial(bus_,Service,path.c_str(),si,prop,nullptr,'u',&n),prop);return n;};s.main=number("MainPID");s.control=number("ControlPID");
 sd_bus_message*m=nullptr;check(sd_bus_get_property(bus_,Service,path.c_str(),ui,"Job",nullptr,&m,"(uo)"),"Job");const char*job=nullptr;check(sd_bus_message_read(m,"(uo)",&s.job,&job),"Job tuple");sd_bus_message_unref(m);m=nullptr;
 check(sd_bus_get_property(bus_,Service,path.c_str(),ui,"InvocationID",nullptr,&m,"ay"),"InvocationID");const void*bytes=nullptr;std::size_t len=0;check(sd_bus_message_read_array(m,'y',&bytes,&len),"InvocationID array");if(len!=16){sd_bus_message_unref(m);throw std::runtime_error("InvocationID size");}for(std::size_t i=0;i<len;++i){auto v=static_cast<const unsigned char*>(bytes)[i];s.invocation+="0123456789abcdef"[v>>4];s.invocation+="0123456789abcdef"[v&15];}sd_bus_message_unref(m);return s;
}
void Bus::transient(const std::string&unit,const std::vector<Property>&props,const std::vector<std::string>&argv,const std::vector<std::string>&post){
 sd_bus_message*m=nullptr,*reply=nullptr;sd_bus_error error=SD_BUS_ERROR_NULL;
 auto open=[&](char t,const char*sig){check(sd_bus_message_open_container(m,t,sig),"open container");};auto close=[&]{check(sd_bus_message_close_container(m),"close container");};
 try{
 check(sd_bus_message_new_method_call(bus_,&m,Service,Path,Manager,"StartTransientUnit"),"new transient");check(sd_bus_message_append(m,"ss",unit.c_str(),"fail"),"unit/mode");open('a',"(sv)");
 for(const auto&p:props){open('r',"sv");check(sd_bus_message_append(m,"s",p.name.c_str()),"property");open('v',p.type.c_str());if(p.type=="s")check(sd_bus_message_append(m,"s",p.value.c_str()),"string");else if(p.type=="b")check(sd_bus_message_append(m,"b",int(p.number)),"bool");else if(p.type=="t")check(sd_bus_message_append(m,"t",p.number),"uint64");else if(p.type=="as"){open('a',"s");check(sd_bus_message_append(m,"s",p.value.c_str()),"array string");close();}else throw std::runtime_error("property type");close();close();}
 auto command=[&](const char*name,const std::vector<std::string>&args){if(args.empty())return;open('r',"sv");check(sd_bus_message_append(m,"s",name),"exec name");open('v',"a(sasb)");open('a',"(sasb)");open('r',"sasb");check(sd_bus_message_append(m,"s",args[0].c_str()),"exec path");open('a',"s");for(const auto&a:args)check(sd_bus_message_append(m,"s",a.c_str()),"exec argv");close();check(sd_bus_message_append(m,"b",0),"exec failure");close();close();close();close();};
 command("ExecStart",argv);command("ExecStopPost",post);close();open('a',"(sa(sv))");close();int rc=sd_bus_call(bus_,m,500000,&error,&reply);if(rc<0)throw std::runtime_error(std::string("StartTransientUnit:")+(error.message?error.message:std::to_string(rc)));sd_bus_message_unref(reply);sd_bus_message_unref(m);sd_bus_error_free(&error);
 }catch(...){sd_bus_message_unref(reply);sd_bus_message_unref(m);sd_bus_error_free(&error);throw;}
}
}
