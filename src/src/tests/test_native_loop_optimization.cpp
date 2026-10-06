#include "core_dll/mbaa_mem/NativeLoopKernels.hpp"
#include "test_support.hpp"
#include <array>
#include <random>
#include <vector>
#ifdef _WIN32
#include "core_dll/hook/DxHook.hpp"
#include "core_dll/mbaa_mem/SteamCodeSignature.hpp"
void HookLog(const char*) {}
HRESULT cccaster::game_interface::DxHook::AdjacentScenePair(LPDIRECT3DDEVICE9) { return 0; }
extern "C" {
void cc_loop_collision_gate();
void cc_loop_live_a_gate();
void cc_loop_live_b_gate();
void cc_loop_live_c_gate();
void cc_loop_live_d_gate();
void cc_loop_sound_gate();
void cc_loop_sound_vector_gate();
void cc_loop_commands_gate();
extern bool cc_loop_trace;
void cc_loop_verify_entry();
void cc_loop_verify_candidate();
extern uint32_t cc_loop_verify_entries,cc_loop_verify_candidates[2],cc_loop_verify_hits[2];
extern uintptr_t cc_loop_verify_resume,cc_loop_verify_native,cc_loop_verify_pool,cc_loop_verify_end;
__attribute__((naked)) unsigned __cdecl CheckCandidate(uint32_t*,uint32_t,uint32_t,uint32_t*) {
    __asm__ __volatile__("movl 8(%esp),%ecx\n\tmovl 12(%esp),%edx\n\t"
        "pushl 4(%esp)\n\tpushl 20(%esp)\n\tcall _cc_loop_verify_candidate\n\taddl $8,%esp\n\tret\n\t");
}
extern uintptr_t cc_loop_pool,cc_loop_collision_body,cc_loop_collision_done,cc_loop_end_a,cc_loop_end_b,cc_loop_end_c,cc_loop_end_d,cc_loop_body_a,cc_loop_body_b,cc_loop_body_c,cc_loop_body_d,cc_loop_done_a,cc_loop_done_b,cc_loop_done_c,cc_loop_done_d,cc_loop_sound_flags,cc_loop_sound_body,cc_loop_sound_done;
struct Registers { uint32_t result, esi, edi, ebp, index, ebx, ecx; };
// 製品と同じnaked bridgeへ入れ、継続先を小さなRETスタブに置き換えてレジスタを採取する。
__attribute__((naked)) void __cdecl CheckGate(void*, uintptr_t, uintptr_t, uint32_t, Registers*) {
    __asm__ __volatile__(
        "pushl %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\tsubl $0x20,%esp\n\t"
        "movl 56(%esp),%esi\n\tmovl %esi,%edi\n\tmovl %esi,%ebp\n\tmovl 60(%esp),%ebx\n\t"
        "movl 64(%esp),%eax\n\tmovl %eax,0x10(%esp)\n\tcall *52(%esp)\n\t"
        "movl 68(%esp),%edx\n\tmovl %eax,0(%edx)\n\tmovl %esi,4(%edx)\n\tmovl %edi,8(%edx)\n\t"
        "movl %ebp,12(%edx)\n\tmovl 0x10(%esp),%eax\n\tmovl %eax,16(%edx)\n\tmovl %ebx,20(%edx)\n\t"
        "addl $0x20,%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
// Steam衝突走査のEBPローカルとEAX/EDX/EDI/ECXの生存値を模擬。
__attribute__((naked)) void __cdecl CheckCollision(void*,uintptr_t,uintptr_t,uint32_t,Registers*) {
    __asm__ __volatile__(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $0xc0,%esp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%edi\n\tmovl 16(%ebp),%eax\n\tmovl 20(%ebp),%edx\n\t"
        "movl %eax,-0xac(%ebp)\n\t"
        "movl $0x123400,%ecx\n\tmovl $1000,%ebx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%esi\n\tmovl %eax,0(%esi)\n\tmovl %edi,8(%esi)\n\t"
        "movl -0xbc(%ebp),%eax\n\tmovl %eax,12(%esi)\n\tmovl %edx,16(%esi)\n\t"
        "movl %ebx,20(%esi)\n\tmovl %ecx,24(%esi)\n\t"
        "popl %edi\n\tpopl %esi\n\tpopl %ebx\n\tmovl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
__attribute__((naked)) unsigned __cdecl CheckCommandGate(uint8_t*, unsigned) {
    __asm__ __volatile__(
        "pushl %ebx\n\tpushl %edi\n\tmovl 12(%esp),%edi\n\tmovl 16(%esp),%ebx\n\t"
        "call 1f\n\tpopl %edi\n\tpopl %ebx\n\tret\n\t"
        "1: pushl %ebp\n\tmovl %esp,%ebp\n\tandl $-8,%esp\n\tpushl %ecx\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "jmp _cc_loop_commands_gate\n\t");
}
}
#endif

namespace nl = cccaster::game_memory::native_loops;
int main(int argc, char**) {
#ifdef _WIN32
    // 固定VAを予約しない。任意配置に解決した製品ブリッジをそのまま実行する。
    auto* mapped=static_cast<uint8_t*>(VirtualAlloc(nullptr,0xf3e000,
        MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    CC_CHECK(mapped!=nullptr);
    if(!mapped) return 1;
    using cccaster::game_memory::GameRuntime;
    CC_CHECK(GameRuntime::Initialize(cccaster::game_build::Edition::Steam20170105,
                                     {uintptr_t(mapped),0xf3e000}));
    const auto va=[](uint32_t address){return GameRuntime::Preferred(address);};
#endif
    std::mt19937 random(0x20261006);
    std::vector<uint8_t> pool(nl::ObjectCount * nl::ObjectStride);
    std::array<uint8_t, nl::ObjectStride> actor{};
    actor[0x2f0] = 1;
    CC_CASE("候補絞込みは元の枠順・未使用・所有者・特殊種別・自己除外を保持する");
    for (unsigned run = 0; run < 150; ++run) {
        for (unsigned i = 0; i < nl::ObjectCount; ++i) {
            auto* slot = pool.data() + i * nl::ObjectStride;
            slot[0] = random() % 8 == 0;
            slot[9] = random() % 5 == 0 ? 0x1f : 0;
            slot[0x2f4] = random() % 4;
        }
        for (unsigned start = 0; start <= nl::ObjectCount; ++start) {
            auto reference = start;
            while (reference < nl::ObjectCount) {
                auto* p = pool.data() + reference * nl::ObjectStride;
                if (p[0] && p[9] != 0x1f && p[0x2f4] != actor[0x2f0]) break;
                ++reference;
            }
            CC_CHECK_EQ(nl::NextCollision(pool.data(), start, actor.data()), reference);
        }
    }
    std::fill(pool.begin(), pool.end(), 0);
    pool[999 * nl::ObjectStride] = 1;
    CC_CHECK_EQ(nl::NextCollision(pool.data(), 999, pool.data() + 999 * nl::ObjectStride + 4), 1000u);
    CC_CASE("更新途中の生成・削除と保存状態の復元を次回の走査へ反映する");
    const auto begin = reinterpret_cast<uintptr_t>(pool.data()) + 4;
    const auto end = begin + pool.size();
    CC_CHECK_EQ(nl::NextLive(begin, end, 4), begin + 999 * nl::ObjectStride);
    pool[5 * nl::ObjectStride] = 1;
    CC_CHECK_EQ(nl::NextLive(begin, end, 4), begin + 5 * nl::ObjectStride);
    pool[5 * nl::ObjectStride] = pool[999 * nl::ObjectStride] = 0;
    CC_CHECK_EQ(nl::NextLive(begin, end, 4), end);

    CC_CASE("SFXは1だけを検出し、未整列・末尾・再生中の後続要求を保持する");
    std::array<uint8_t, 1532> sound{};
    for (unsigned alignment = 0; alignment < 16; ++alignment) {
        auto flags = std::span(sound).subspan(alignment, nl::SoundCount);
        for (auto& v : flags) v = uint8_t(random());
        for (unsigned start = 0; start <= flags.size(); ++start) {
#if defined(__i386__) || defined(__x86_64__)
            CC_CHECK_EQ(nl::NextSoundVector(flags, start), nl::NextSoundScalar(flags, start));
#endif
        }
        std::fill(flags.begin(), flags.end(), 0);
        flags[1499] = 1;
        CC_CHECK_EQ(nl::NextSoundVector(flags, 0), 1499u);
        flags[15] = 1;
        CC_CHECK_EQ(nl::NextSoundVector(flags, 0), 15u);
        flags[16] = 1; // 先の検索で同じベクトルへ読んでいても次回に再取得する。
        CC_CHECK_EQ(nl::NextSoundVector(flags, 16), 16u);
    }

    CC_CASE("命令が状態・命令列・件数を変更しても元のindexで続行する");
    std::array<unsigned, 7> entries{1,2,3,4,5,6,7};
    std::array<std::vector<unsigned*>, 2> lists{{{&entries[0], nullptr, &entries[2]},
                                              {&entries[6], &entries[1], &entries[3], &entries[4]}}};
    unsigned current = 0;
    std::vector<unsigned> executed;
    nl::RunCommands(2, [&] { return current; }, [&](unsigned s) { return lists[s].size(); },
        [&](unsigned s, unsigned i) { return lists[s][i]; }, [](unsigned* p) { return *p == 4 ? 1 : 0xff; },
        [&](unsigned* p) { executed.push_back(*p); if (*p == 1) current = 1; if (*p == 2) lists[1].push_back(&entries[5]); });
    CC_CHECK(executed == std::vector<unsigned>({1,2,5,6}));

#ifdef _WIN32
    CC_CASE("32bitブリッジのスタック・レジスタ・継続先を実行して確認する");
    CC_CASE("PE HIGHLOWだけを正規化し、再配置以外の変更を拒否する");
    const std::array<uint16_t,2> offsets{1,6};
    const std::array<uint8_t,11> original{0xa1,0x14,0x44,0x5c,0,0x68,0xb0,0x47,0x7d,0,0xc3};
    const auto expected=cccaster::game_memory::steam_code::NormalizedHash(original,0,offsets);
    for(uint32_t delta : {0u,0xffdc0000u,0x13000000u}) {
        auto bytes=original;
        for(auto offset:offsets) {uint32_t value;std::memcpy(&value,bytes.data()+offset,4);
            value+=delta;std::memcpy(bytes.data()+offset,&value,4);}
        CC_CHECK_EQ(cccaster::game_memory::steam_code::NormalizedHash(bytes,delta,offsets),expected);
        bytes[5]^=1;
        CC_CHECK(cccaster::game_memory::steam_code::NormalizedHash(bytes,delta,offsets)!=expected);
    }
    auto continuation = [](uintptr_t p, uint32_t result) {
        auto* code = reinterpret_cast<uint8_t*>(p); code[0] = 0xb8;
        std::memcpy(code + 1, &result, 4); code[5] = 0xc3;
    };
    cc_loop_pool=va(0x6e28f0);
    cc_loop_collision_body=va(0x4c7455);
    cc_loop_collision_done=va(0x4c7516);
    cc_loop_end_a=va(0x7accca);
    cc_loop_end_b=va(0x7ace1c);
    cc_loop_end_c=va(0x7acb54);
    cc_loop_end_d=va(0x7acb54);
    cc_loop_body_a=va(0x4aaa29);
    cc_loop_body_b=va(0x4aaa8c);
    cc_loop_body_c=va(0x4aaafc);
    cc_loop_body_d=va(0x4aab3c);
    cc_loop_done_a=va(0x4aaa7e);
    cc_loop_done_b=va(0x4aaadd);
    cc_loop_done_c=va(0x4aab27);
    cc_loop_done_d=va(0x4aab5a);
    cc_loop_sound_flags=va(0x7d47b0);
    cc_loop_sound_body=va(0x52195f);
    cc_loop_sound_done=va(0x521979);

    for(const auto p:{cc_loop_collision_body,cc_loop_body_a,cc_loop_body_b,cc_loop_body_c,cc_loop_body_d,cc_loop_sound_body}) continuation(p,1);
    for(const auto p:{cc_loop_collision_done,cc_loop_done_a,cc_loop_done_b,cc_loop_done_c,cc_loop_done_d,cc_loop_sound_done}) continuation(p,0);
    FlushInstructionCache(GetCurrentProcess(),mapped,0xf3e000);
    auto* fixedPool=reinterpret_cast<uint8_t*>(cc_loop_pool);
    for (bool tracing : {false, true}) {
    cc_loop_trace = tracing;
    std::memset(fixedPool, 0, nl::ObjectCount * nl::ObjectStride);
    Registers regs{};
    auto collision = reinterpret_cast<void*>(&cc_loop_collision_gate);
    CheckCollision(collision, cc_loop_pool+4, uintptr_t(actor.data()), 0, &regs);
    CC_CHECK_EQ(regs.result, 0u); CC_CHECK_EQ(regs.index, 1000u);
    fixedPool[777 * nl::ObjectStride] = 1;
    CheckCollision(collision, cc_loop_pool+4, uintptr_t(actor.data()), 0, &regs);
    CC_CHECK_EQ(regs.result, 1u); CC_CHECK_EQ(regs.index, 777u);
    CC_CHECK_EQ(regs.edi,cc_loop_pool+4+777*nl::ObjectStride);
    CC_CHECK_EQ(regs.ebp,777u); CC_CHECK_EQ(regs.ecx,0x123400u+777*0xafc);
    CC_CHECK_EQ(regs.ebx,1000u);
    struct Gate { void* gate; uint32_t start, offset; unsigned reg; };
    for (const auto& g : std::array<Gate,4>{{
            {reinterpret_cast<void*>(&cc_loop_live_a_gate),uint32_t(cc_loop_pool+0x17a),0x17a,1},
            {reinterpret_cast<void*>(&cc_loop_live_b_gate),uint32_t(cc_loop_pool+0x2cc),0x2cc,1},
            {reinterpret_cast<void*>(&cc_loop_live_c_gate),uint32_t(cc_loop_pool+4),4,1},
            {reinterpret_cast<void*>(&cc_loop_live_d_gate),uint32_t(cc_loop_pool+4),4,1}}}) {
        CheckGate(g.gate, g.start, 0x12345678, 0, &regs);
        CC_CHECK_EQ(regs.result, 1u);
        CC_CHECK_EQ(reinterpret_cast<uint32_t*>(&regs)[g.reg], g.start + 777 * nl::ObjectStride);
        CheckGate(g.gate, g.start + 778 * nl::ObjectStride, 0x12345678, 0, &regs);
        CC_CHECK_EQ(regs.result, 0u);
        CC_CHECK_EQ(reinterpret_cast<uint32_t*>(&regs)[g.reg], g.start + 1000 * nl::ObjectStride);
        CC_CHECK_EQ(regs.ebx, 0x12345678u);
    }
    auto* fixedFlags = reinterpret_cast<uint8_t*>(cc_loop_sound_flags);
    std::memset(fixedFlags, 0, nl::SoundCount);
    fixedFlags[1499] = 1;
    CheckGate(reinterpret_cast<void*>(&cc_loop_sound_gate), 0, 0x12345678, 0, &regs);
    CC_CHECK_EQ(regs.result, 1u); CC_CHECK_EQ(regs.esi, 1499u);
    CheckGate(reinterpret_cast<void*>(&cc_loop_sound_gate), 1500, 0x12345678, 0, &regs);
    CC_CHECK_EQ(regs.result, 0u); CC_CHECK_EQ(regs.esi, 1500u);
    for (unsigned run = 0; run < 16; ++run) {
        for (unsigned i = 0; i < nl::SoundCount; ++i) fixedFlags[i] = uint8_t(random());
        const std::span<const uint8_t> flags(fixedFlags, nl::SoundCount);
        for (unsigned first = 0; first <= nl::SoundCount; ++first) {
            const auto next = nl::NextSoundScalar(flags, first);
            CheckGate(reinterpret_cast<void*>(&cc_loop_sound_vector_gate), first, 0x12345678, 0, &regs);
            CC_CHECK_EQ(regs.esi, next);
            CC_CHECK_EQ(regs.result, unsigned(next < nl::SoundCount));
            CC_CHECK_EQ(regs.ebx, 0x12345678u);
        }
    }
    // 空の命令状態でも元のプロローグ/エピローグで呼出し元へ戻れること。
    std::array<uint8_t,0x50> state{};
    auto* ptr = state.data(); std::memcpy(actor.data() + 0x31c, &ptr, sizeof(ptr));
    CC_CHECK_EQ(CheckCommandGate(actor.data(), 2), 0u);
    }
    cc_loop_trace = false;
    CC_CASE("衝突の診断ブリッジは元の引数・戻り値・iteratorを保持する");
    const uint8_t entryEnd[]{0x81,0xc4,0xdc,0,0,0,0xb8,3,0,0,0,0xc3};
    cc_loop_verify_resume=cc_loop_pool+0xd0000;
    cc_loop_verify_native=cc_loop_verify_resume+0x100;
    cc_loop_verify_pool=cc_loop_pool+4;cc_loop_verify_end=cc_loop_verify_pool+nl::ObjectCount*nl::ObjectStride;
    std::memcpy(reinterpret_cast<void*>(cc_loop_verify_resume),entryEnd,sizeof(entryEnd));
    // fake native: ECX+EDX -> out, iterator count++, return iterator[1].
    const uint8_t candidateBody[]{0x01,0xd1,0x8b,0x44,0x24,4,0x89,0x08,
        0x8b,0x54,0x24,8,0xff,0x02,0x8b,0x42,4,0xc3};
    std::memcpy(reinterpret_cast<void*>(cc_loop_verify_native),candidateBody,sizeof(candidateBody));
    FlushInstructionCache(GetCurrentProcess(),mapped,0xf3e000);
    CC_CHECK_EQ(reinterpret_cast<unsigned(*)()>(&cc_loop_verify_entry)(), 3u);
    CC_CHECK_EQ(cc_loop_verify_entries, 1u);
    uint32_t output = 0;
    uint32_t iterator[]{0,0,uint32_t(cc_loop_pool-4),0};
    CC_CHECK_EQ(CheckCandidate(iterator, 123, 456, &output), 0u);
    CC_CHECK_EQ(output, 579u); CC_CHECK_EQ(iterator[0], 1u);
    iterator[1] = 1; iterator[2] = cc_loop_verify_pool;
    CC_CHECK_EQ(CheckCandidate(iterator, 7, 11, &output), 1u);
    CC_CHECK_EQ(output, 18u); CC_CHECK_EQ(iterator[0], 2u);
    CC_CHECK_EQ(cc_loop_verify_candidates[0], 1u); CC_CHECK_EQ(cc_loop_verify_candidates[1], 1u);
    CC_CHECK_EQ(cc_loop_verify_hits[0], 0u); CC_CHECK_EQ(cc_loop_verify_hits[1], 1u);
    VirtualFree(mapped, 0, MEM_RELEASE);
#endif
    return cccaster::test::Summarize("native_loop_optimization");
}
