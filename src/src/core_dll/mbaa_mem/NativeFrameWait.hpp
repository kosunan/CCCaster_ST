#pragma once
#include "shared_contracts/CheckedPatch.hpp"
#include "GameRuntime.hpp"

namespace cccaster::game_memory::native_frame_wait {
// Steam: 映画確認・実時間更新・通信不足returnを保持し、3つの待機分岐だけを外す。
inline constexpr uintptr_t SleepSite=0x476f33, LoopSite=0x4771d7, PendingSite=0x47711a;
inline constexpr uint8_t SleepOriginal[]{0x85,0xc0,0x74,0x04,0x6a,0x08,0xeb,0x02,0x6a,0x02,0xff,0x15,0x5c,0xb0,0x56,0x00,0xf3,0x0f,0x10,0x4c,0x24,0x0c,0x8d,0x44,0x24,0x10,0x50,0x8b,0xce,0xe8,0x9b,0x00,0x00,0x00};
inline constexpr uint8_t LoopOriginal[]{0x0f,0x85,0x97,0x01,0x00,0x00};
inline constexpr uint8_t PendingOriginal[]{0x6a,0x08,0xff,0x15,0x5c,0xb0,0x56,0x00};
inline constexpr uint8_t Gate[]{0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x14,0x53,0x56,0x8b,0xf1,0xf3,0x0f,0x11,0x4c,0x24,0x08,0x57,0x83,0x7e,0x4c,0x01,0x75,0x11,0x6a,0x08,0xff,0x15,0x5c,0xb0,0x56,0x00,0x33,0xc0,0x5f,0x5e,0x5b,0x8b,0xe5,0x5d,0xc3};
inline constexpr uint8_t Continue[]{0xa1,0x30,0x4d,0x5b,0x00,0x3b,0x05,0xb4,0xa9,0x5c,0x00,0x75,0x05,0xe8,0xba,0x31,0x07,0x00,0x5f,0x5e,0xb8,0x01,0x00,0x00,0x00,0x5b,0x8b,0xe5,0x5d,0xc3};
inline constexpr uint8_t SleepJump[]{0xeb,0x0e};
inline constexpr uint8_t LoopJump[]{0xe9,0x98,0x01,0,0,0x90};
inline constexpr uint8_t PendingJump[]{0xeb,0x06};
// 配列内のPE再配置スロットだけを実ロード先へ移す。
template<size_t N> std::array<uint8_t,N> Relocate(const uint8_t (&bytes)[N], std::initializer_list<size_t> offsets) {
    std::array<uint8_t,N> result; std::memcpy(result.data(),bytes,N);
    for(auto offset:offsets) { uint32_t value; std::memcpy(&value,result.data()+offset,4);
        value+=uint32_t(GameRuntime::Image().base-0x400000); std::memcpy(result.data()+offset,&value,4); }
    return result;
}
// ゲーム入口停止中、または対象関数の外側のゲームスレッドで適用する。
template<class Memory> patch::Result Apply(Memory& memory) {
    if(!GameRuntime::IsSteam()) return {patch::Error::Mismatch,0,0,"frame_wait_build"};
    const auto sleep=Relocate(SleepOriginal,{12});
    const auto pending=Relocate(PendingOriginal,{4});
    const auto gate=Relocate(Gate,{30});
    const auto continuation=Relocate(Continue,{1,7});
    struct Guard {uintptr_t address; std::span<const uint8_t> bytes;};
    for(const auto& guard:{Guard{0x477100,gate},Guard{0x477374,continuation}}) {
        std::array<uint8_t,64> actual{}; const auto address=GameRuntime::Preferred(guard.address,guard.bytes.size());
        if(!address || !memory.Read(address,actual.data(),guard.bytes.size()))
            return {patch::Error::Read,memory.LastError(),address,"frame_wait_context"};
        if(std::memcmp(actual.data(),guard.bytes.data(),guard.bytes.size()))
            return {patch::Error::Mismatch,0,address,"frame_wait_context"};
    }
    const patch::Spec specs[]{
        {"frame_wait_sleep",GameRuntime::Preferred(SleepSite),sleep,SleepJump},
        {"frame_wait_loop",GameRuntime::Preferred(LoopSite),LoopOriginal,LoopJump},
        {"frame_wait_pending",GameRuntime::Preferred(PendingSite),pending,PendingJump}};
    return patch::Apply(memory,specs);
}
patch::Result Enable();
}
