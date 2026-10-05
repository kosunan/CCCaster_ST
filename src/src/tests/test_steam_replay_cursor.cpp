#include "core_dll/mbaa_mem/SteamReplayCursor.hpp"
#include <cstdio>
#include <vector>
using namespace cccaster::mbaa::steam::replay;
int main() {
 auto image=(unsigned char*)VirtualAlloc(nullptr,0xf3e000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 if(!image) return 2;
 cccaster::game_memory::GameRuntime::Initialize(cccaster::game_build::Edition::Steam20170105,{uintptr_t(image),0xf3e000});
 Round rounds[10]{}; Container inputs[4]{}; State states[4][8]{}; uint32_t rng[8]={10,20,30,40};
 auto slots=(uintptr_t*)(image+0x3e9bfc); slots[0]=uintptr_t(rounds); slots[1]=uintptr_t(rounds+10); slots[2]=slots[1]; slots[3]=2;
 auto &round=rounds[2]; round.inputs=inputs;round.inputsEnd=inputs+4;round.inputsCapacity=inputs+4;
 round.rng=rng;round.rngEnd=rng+2;round.rngCapacity=rng+8;
 for(int i=0;i<4;++i){inputs[i]={states[i],states[i]+2,states[i]+8,4,4,1,0};states[i][1].bytes[1]=2;}
 int failed=0,checks=0;auto check=[&](bool v,const char*s){++checks;if(!v){++failed;std::printf("FAIL %s\n",s);}};
 Snapshot saved;check(Capture(saved),"capture selected round in 10-round vector");check(saved.round==3,"uses currentRoundIndex");
 State reallocated[4][12]{};uint32_t rngReallocated[12]={10,20,30,40};
 for(int i=0;i<4;++i){std::memcpy(reallocated[i],states[i],sizeof(states[i]));inputs[i].states=reallocated[i];inputs[i].end=reallocated[i]+4;inputs[i].capacity=reallocated[i]+12;inputs[i].index=3;inputs[i].total=7;inputs[i].total2=7;reallocated[i][1].bytes[1]=5;reallocated[i][3].bytes[0]=9;}
 round.rng=rngReallocated;round.rngEnd=rngReallocated+4;round.rngCapacity=rngReallocated+12;
 auto invalid=saved;invalid.players[3].endOffset=100;check(!Restore(invalid),"reject invalid fourth cursor");check(inputs[0].total==7 && reallocated[0][1].bytes[1]==5 && round.rngEnd==rngReallocated+4,"no earlier cursor writes on rejection");
 check(Restore(saved),"restore across buffer reallocation");
 for(int i=0;i<4;++i) check(inputs[i].states==reallocated[i]&&inputs[i].end==reallocated[i]+2&&inputs[i].index==1&&inputs[i].total==4&&reallocated[i][1].bytes[1]==2&&reallocated[i][3].bytes[0]==0,"restore compressed state and truncate using current buffer");
 check(round.rng==rngReallocated&&round.rngEnd==rngReallocated+2&&rngReallocated[2]==0,"restore RNG end using current buffer");
 for(int i=0;i<4;++i){inputs[i].index=2;inputs[i].total=99;inputs[i].frameInState=7;}
 Snapshot playback;check(!Capture(playback),"finished playback is invalid for rollback capture");
 check(Capture(playback,true) && Restore(playback,true),"restart completed dummy recording");
 for(int i=0;i<4;++i) check(inputs[i].index==0 && inputs[i].total==0 && inputs[i].total2==4 && inputs[i].frameInState==0 && inputs[i].end==reallocated[i]+2 && reallocated[i][1].bytes[1]==2,"dummy rewind retains recording and total length");
 check(round.rngEnd==rngReallocated+2,"dummy rewind retains RNG recording");
 slots[3]=1;check(!Restore(saved),"reject different current round");slots[3]=10;Snapshot empty;check(Capture(empty) && empty.round==11,"capture uncreated next round");
 slots[3]=11;check(!Capture(saved),"reject index beyond count");
 slots[3]=0;slots[1]=slots[0];check(Capture(empty) && empty.round==1,"empty first intro keeps logical index");
 slots[1]=uintptr_t(rounds+1);rounds[0]=round;check(Restore(empty),"rewind newly allocated first round to empty streams");
 check(inputs[0].end==inputs[0].states && rounds[0].rngEnd==rounds[0].rng,"first intro truncates inputs and RNG");
 check(!Restore(saved),"different logical round remains rejected");slots[3]=0xffffffff;check(!Capture(saved),"reject negative index");
 std::printf("checks=%d failed=%d\n",checks,failed);return failed?1:0;
}
