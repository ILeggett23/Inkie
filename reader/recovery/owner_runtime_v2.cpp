// SPDX-License-Identifier: AGPL-3.0-only
#include "owner_runtime_v2.h"
#include <unistd.h>
#include <stdexcept>
#include <time.h>
namespace recovery::v2::runtime {
namespace { ClockPair afterObservation(std::uint64_t generation){timespec m{},b{};if(clock_gettime(CLOCK_MONOTONIC,&m)||clock_gettime(CLOCK_BOOTTIME,&b))throw std::runtime_error("owner paired clock observation");return {Ns(m.tv_sec)*1000000000+Ns(m.tv_nsec),Ns(b.tv_sec)*1000000000+Ns(b.tv_nsec),generation};} }
Owner::Owner(Manager& m,SessionIdentity id,Profile p,std::uint64_t generation,ClockPair start,int childFd,std::uint64_t childConnection,int fuseFd,std::uint64_t fuseConnection,std::uint32_t fusePid):manager_(m),identity_(id),profile_(std::move(p)),generation_(generation),started_(start),control_(id,generation,profile_.timing,start,childFd,childConnection,0,fuseFd,fuseConnection,0,fusePid){if(!valid(profile_)||!manager_.verify()||!manager_.alive())throw std::runtime_error("owner runtime original manager profile required");}
bool Owner::poll(ClockPair now,bool power,const Nonce* entropy)noexcept{try{auto authority=manager_.authority(identity_,power);auto generation=now.generation;auto sample=[](void* context)noexcept{try{return afterObservation(*static_cast<std::uint64_t*>(context));}catch(...){return ClockPair{};}};bool live=control_.pollObserved(authority,entropy,sample,&generation);if(!live)close(afterObservation(generation));return live;}catch(...){close(now);return false;}}
bool Owner::admitted(ClockPair now,bool power)noexcept{try{auto authority=manager_.authority(identity_,power);now=afterObservation(now.generation);bool result=control_.admitted(now,authority);if(control_.closed())close(now);return result;}catch(...){close(now);return false;}}
void Owner::close(ClockPair now)noexcept{control_.close(now);if(terminationRequested_)return;const auto& grant=control_.grant();if(closure()&&closure()->clockReliable&&grant.expires.monotonic&&now.generation==grant.expires.generation&&now.monotonic<grant.expires.monotonic&&now.boottime<grant.expires.boottime)return;terminationRequested_=true;try{if(manager_.alive())manager_.stopReader();}catch(...){/* No retry, replacement reference or stock start. The fuse is independent. */}}
bool Owner::beginWitness(int fd,ClockPair now)noexcept{
 if(witnessAttempted_||!closed()||!closure()){if(fd>=0)::close(fd);return false;}witnessAttempted_=true;
 try{auto before=manager_.state(Unit::Reader);if(before.invocation!=identity_.invocation.data()||before.control<2){::close(fd);return false;}
  witness_=std::make_unique<control::Channel>(fd,manager_.connection(),0,before.control);fd=-1;if(!witness_->valid())return false;
  authentication_.peerUid=witness_->peer().uid;authentication_.peerPid=witness_->peer().pid;authentication_.kernelCredentialsVerified=witness_->peer().verified;authentication_.controlBefore=before.control;authentication_.invocationBefore=before.invocation;authentication_.originalConnection=manager_.connection();authentication_.sameOriginalConnection=manager_.alive();
  bootstrap::Packet offer;offer.kind=bootstrap::Kind::WitnessOffer;offer.identity=identity_;text(offer.profile,profile_.qualification);offer.generation=generation_;offer.started=started_;offer.closure=*closure();Wire wire;
  if(!proofWithinBudget(*closure(),identity_,generation_,control_.grantSequence(),now,profile_.timing)||!bootstrap::encode(offer,wire)||!witness_->send(wire)){witness_->seal();return false;}return true;
 }catch(...){if(fd>=0)::close(fd);if(witness_)witness_->seal();return false;}
}
bool Owner::pollWitness(ClockPair now)noexcept{
 if(receiptReady_)return true;if(!witness_||!witness_->valid()||!closure())return false;
 try{if(!proofWithinBudget(*closure(),identity_,generation_,control_.grantSequence(),now,profile_.timing)){witness_->seal();return false;}Wire wire;auto read=witness_->receive(wire);if(read==control::Read::Empty)return false;if(read!=control::Read::Packet)return false;auto packet=bootstrap::decode(wire);
  if(!packet||packet->kind!=bootstrap::Kind::Receipt||packet->identity!=identity_||packet->generation!=generation_||packet->started!=started_||packet->profile.data()!=profile_.qualification||packet->closure!=*closure()){witness_->seal();return false;}
  receipt_={packet->closure,packet->witnessPid,packet->drained};auto after=manager_.state(Unit::Reader);authentication_.controlAfter=after.control;authentication_.invocationAfter=after.invocation;authentication_.sameOriginalConnection=authentication_.sameOriginalConnection&&manager_.alive();receiptReady_=witness::authenticated(identity_,receipt_,authentication_);if(receiptReady_){packet->kind=bootstrap::Kind::WitnessAck;Wire ack;receiptReady_=bootstrap::encode(*packet,ack)&&witness_->send(ack);}witness_->seal();return receiptReady_;
 }catch(...){witness_->seal();return false;}
}
bool Owner::restore(ClockPair now)noexcept{if(!receiptReady_||!closed()||!closure())return false;try{return manager_.restore(identity_,*closure(),receipt_,authentication_,now);}catch(...){return false;}}
}
