#include "NativeLoopOptimization.hpp"
#include "NativeLoopKernels.hpp"
#include "NativeLoopSignatures.hpp"
#include "SteamCodeSignature.hpp"
#include "SteamV151Signatures.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/mbaa_mem/GameBuildGuard.hpp"
#include "core_dll/hook/DxHook.hpp"
#include "core_dll/hook/ScenePairMerge.hpp"
#include "shared_contracts/ProcessMemory.hpp"
#include <array>
#include <cstdlib>
#include <vector>

// 診断専用。Steam 4C7360の元の形状判定を呼び、候補とヒットを計数する。
extern "C" {
uint32_t cc_loop_verify_entries=0, cc_loop_verify_candidates[2]{}, cc_loop_verify_hits[2]{};
uintptr_t cc_loop_verify_resume=0, cc_loop_verify_native=0, cc_loop_verify_pool=0, cc_loop_verify_end=0;
__attribute__((naked)) void cc_loop_verify_entry() {
    __asm__ __volatile__("pushfl\n\tincl _cc_loop_verify_entries\n\tpopfl\n\t"
        "subl $0xdc,%esp\n\tjmp *_cc_loop_verify_resume\n\t");
}
__attribute__((naked)) void cc_loop_verify_candidate() {
    // ECX/EDX＋2個のスタック引数。第2スタック引数が形状iterator。
    __asm__ __volatile__("pushl %ebx\n\tmovl 12(%esp),%ebx\n\t"
        "pushl 12(%esp)\n\tpushl 12(%esp)\n\tcall *_cc_loop_verify_native\n\taddl $8,%esp\n\t"
        "movl 8(%ebx),%ecx\n\txorl %edx,%edx\n\tcmpl _cc_loop_verify_pool,%ecx\n\tjb 1f\n\t"
        "cmpl _cc_loop_verify_end,%ecx\n\tjae 1f\n\tincl %edx\n\t"
        "1: incl _cc_loop_verify_candidates(,%edx,4)\n\ttestl %eax,%eax\n\tje 2f\n\t"
        "incl _cc_loop_verify_hits(,%edx,4)\n\t2: popl %ebx\n\tret\n\t");
}
}

namespace nl = cccaster::game_memory::native_loops;
using cccaster::game_memory::GameRuntime;
extern "C" {
bool cc_loop_trace = false;
uintptr_t cc_loop_pool = 0;
uintptr_t cc_loop_collision_body = 0;
uintptr_t cc_loop_collision_done = 0;
uintptr_t cc_loop_end_a = 0;
uintptr_t cc_loop_end_b = 0;
uintptr_t cc_loop_end_c = 0;
uintptr_t cc_loop_end_d = 0;
uintptr_t cc_loop_body_a = 0;
uintptr_t cc_loop_body_b = 0;
uintptr_t cc_loop_body_c = 0;
uintptr_t cc_loop_body_d = 0;
uintptr_t cc_loop_done_a = 0;
uintptr_t cc_loop_done_b = 0;
uintptr_t cc_loop_done_c = 0;
uintptr_t cc_loop_done_d = 0;
uintptr_t cc_loop_sound_flags = 0;
uintptr_t cc_loop_sound_body = 0;
uintptr_t cc_loop_sound_done = 0;
uintptr_t cc_loop_scene_done = 0;
}

namespace {
bool vectorSound = false;
// 元関数全体の署名は自分の最適化パッチを置く前に確定する。
bool collisionValidated = false;
std::array<uint64_t, 8> calls{}, skipped{};
unsigned frames = 0;
void Verify() {
    static const bool requested = std::getenv("CCCASTER_NATIVE_LOOP_VERIFY") != nullptr;
    if (!requested) return;
    static bool attempted = false, installed = false;
    static unsigned sample = 0, maxLive = 0;
    if (!attempted) {
        attempted = true;
        if (!cccaster::game_build::RuntimeValidated() ||
            (!collisionValidated && !cccaster::game_memory::steam_code::Matches(cccaster::game_memory::steam_v151::VerifyCollision))) return;
        constexpr uint8_t entry[]{0x81,0xec,0xdc,0,0,0}, call[]{0xe8,0xce,0xf2,0xff,0xff};
        std::array<uint8_t,6> jump{0xe9,0,0,0,0,0x90};
        std::array<uint8_t,5> replacement{0xe8,0,0,0,0};
        cc_loop_verify_resume=GameRuntime::Preferred(0x4c7369);
        cc_loop_verify_native=GameRuntime::Preferred(0x4c67b0);
        cc_loop_verify_pool=GameRuntime::Preferred(0x6e28f4);
        cc_loop_verify_end=cc_loop_verify_pool+nl::ObjectCount*nl::ObjectStride;
        auto relative=uint32_t(uintptr_t(&cc_loop_verify_entry)-GameRuntime::Preferred(0x4c7368));
        std::memcpy(jump.data()+1,&relative,4);
        relative=uint32_t(uintptr_t(&cc_loop_verify_candidate)-GameRuntime::Preferred(0x4c74e2));
        std::memcpy(replacement.data()+1,&relative,4);
        const std::array<cccaster::patch::Spec,2> specs{{
            {"verify_collision_entry",GameRuntime::Preferred(0x4c7363),entry,jump},
            {"verify_collision_candidate",GameRuntime::Preferred(0x4c74dd),call,replacement}}};
        const auto result = cccaster::patch::Apply(specs);
        installed = bool(result);
        cccaster::domain::session::DebugLog("[NativeVerifyInstall] enabled=%u error=%s", unsigned(installed), cccaster::patch::Name(result.error));
        if (result.rollbackFailed) ExitProcess(ERROR_WRITE_FAULT);
    }
    if (!installed) return;
    unsigned live = 0;
    for (unsigned i = 0; i < nl::ObjectCount; ++i)
        live += reinterpret_cast<const uint8_t*>(GameRuntime::Preferred(0x6e28f0))[i*nl::ObjectStride] != 0;
    maxLive = (std::max)(maxLive, live);
    if (++sample % 120 == 0)
        cccaster::domain::session::DebugLog("[NativeVerify] sample=%u entries=%u players=%u objects=%u playerHits=%u objectHits=%u live=%u maxLive=%u",
            sample, cc_loop_verify_entries, cc_loop_verify_candidates[0], cc_loop_verify_candidates[1],
            cc_loop_verify_hits[0], cc_loop_verify_hits[1], live, maxLive);
}
void Count(unsigned group, uint32_t amount = 0) {
    if (cc_loop_trace) { ++calls[group]; skipped[group] += amount; }
}

}

extern "C" {
__attribute__((force_align_arg_pointer)) uint32_t __cdecl cc_loop_collision(uint32_t first, const uint8_t* actor) {
    const auto next = nl::NextCollision(reinterpret_cast<const uint8_t*>(cc_loop_pool), first, actor);
    Count(0, next - first);
    return next;
}
// Steam 4C744B (vB 46F3B8): EDI=object、EDX=index、[EBP-AC]=actor。
// プレイヤー4枠は元のまま。類似する4C7220の攻撃判定は変更しない。
// EBPのローカル変数と、元ループのECX増分も保持する。
__attribute__((naked)) void cc_loop_collision_gate() {
    __asm__ __volatile__(
        "cmpb $0,-4(%edi)\n\t"
        "jne 3f\n\t"
        "cmpb $0,_cc_loop_trace\n\t"
        "jne 4f\n\t"
        "5: incl %edx\n\t"
        "addl $0xafc,%ecx\n\t"
        "addl $0x33c,%edi\n\t"
        "cmpl $1000,%edx\n\t"
        "jae 1f\n\t"
        "cmpb $0,-4(%edi)\n\t"
        "je 5b\n\t"
        "jmp 3f\n\t"
        "4: pushfl\n\t"
        "pushal\n\t"
        "pushl -0xac(%ebp)\n\t"
        "pushl %edx\n\t"
        "call _cc_loop_collision\n\t"
        "addl $8,%esp\n\t"
        "movl %eax,%edx\n\t"
        "subl 20(%esp),%edx\n\t"
        "imull $0xafc,%edx,%edx\n\t"
        "addl %edx,24(%esp)\n\t"
        "movl %eax,20(%esp)\n\t"
        "imull $0x33c,%eax,%edx\n\t"
        "addl _cc_loop_pool,%edx\n\t"
        "addl $4,%edx\n\t"
        "movl %edx,0(%esp)\n\t"
        "popal\n\t"
        "popfl\n\t"
        "cmpl $1000,%edx\n\t"
        "jae 1f\n\t"
        "3: movl %edx,-0xbc(%ebp)\n\t"
        "movl %ecx,-0xb8(%ebp)\n\t"
        "jmp *_cc_loop_collision_body\n\t"
        "1: movl %edx,-0xbc(%ebp)\n\t"
        "movl %ecx,-0xb8(%ebp)\n\t"
        "jmp *_cc_loop_collision_done\n\t"
    );
}

// 使用中の枠は元コードへ即座に戻す。空き枠が連続するときだけまとめて進める。
#define CC_LIVE_HELPER(name, end, offset, group) \
__attribute__((force_align_arg_pointer)) uintptr_t __cdecl name(uintptr_t cursor) { \
    const auto next = nl::NextLive(cursor, end, offset); \
    Count(group, (next - cursor) / nl::ObjectStride); return next; }
CC_LIVE_HELPER(cc_loop_live_a, cc_loop_end_a, 0x17a, 2)
CC_LIVE_HELPER(cc_loop_live_b, cc_loop_end_b, 0x2cc, 3)
CC_LIVE_HELPER(cc_loop_live_c, cc_loop_end_c, 4, 4)
CC_LIVE_HELPER(cc_loop_live_d, cc_loop_end_d, 4, 5)
#undef CC_LIVE_HELPER

// Steamの4走査はすべてESIがカーソル。ASLR解決済みの終端・継続先だけを使う。
#define CC_LIVE_GATE(name, helper, offset, suffix) \
__attribute__((naked)) void name() { \
    __asm__ __volatile__( \
        "cmpb $0," offset "(%esi)\n\tjne 2f\n\tcmpb $0,_cc_loop_trace\n\tjne 4f\n\t" \
        "3: addl $0x33c,%esi\n\tcmpl _cc_loop_end_" suffix ",%esi\n\tjae 1f\n\t" \
        "cmpb $0," offset "(%esi)\n\tje 3b\n\tjmp 2f\n\t" \
        "4: pushfl\n\tpushal\n\tpushl %esi\n\tcall _" #helper "\n\taddl $4,%esp\n\t" \
        "movl %eax,4(%esp)\n\tpopal\n\tpopfl\n\tcmpl _cc_loop_end_" suffix ",%esi\n\tjae 1f\n\t" \
        "2: jmp *_cc_loop_body_" suffix "\n\t1: jmp *_cc_loop_done_" suffix "\n\t"); }
CC_LIVE_GATE(cc_loop_live_a_gate, cc_loop_live_a, "-0x17a", "a")
CC_LIVE_GATE(cc_loop_live_b_gate, cc_loop_live_b, "-0x2cc", "b")
CC_LIVE_GATE(cc_loop_live_c_gate, cc_loop_live_c, "-4", "c")
CC_LIVE_GATE(cc_loop_live_d_gate, cc_loop_live_d, "-4", "d")
#undef CC_LIVE_GATE

__attribute__((force_align_arg_pointer)) void __cdecl cc_loop_commands(uint8_t* actor, unsigned phase) {
    Count(6);
    nl::RunCommands(phase,
        [=] { return *reinterpret_cast<const uint8_t* const*>(actor + 0x31c); },
        [](const uint8_t* state) { return state[0x40]; },
        [](const uint8_t* state, unsigned i) { return (*reinterpret_cast<const uint32_t* const* const*>(state + 0x44))[i]; },
        [](const uint32_t* entry) { return reinterpret_cast<const uint8_t*>(GameRuntime::Preferred(0x5729e0))[*entry]; },
        [=](const uint32_t* entry) { reinterpret_cast<void (__fastcall*)(uint8_t*, const uint32_t*)>(GameRuntime::Preferred(0x4c0a50))(actor, entry); });
}
// Steam 4BFB0D。ポーズ/スロー判定後。EDI=actor、EBX=phase。
__attribute__((naked)) void cc_loop_commands_gate() {
    __asm__ __volatile__(
        "movl 0x31c(%edi),%eax\n\t"
        "cmpb $0,0x40(%eax)\n\t"
        "je 1f\n\t"
        "pushl %ebx\n\t"
        "pushl %edi\n\t"
        "call _cc_loop_commands\n\t"
        "addl $8,%esp\n\t"
        "1: popl %edi\n\t"
        "popl %esi\n\t"
        "xorl %eax,%eax\n\t"
        "popl %ebx\n\t"
        "movl %ebp,%esp\n\t"
        "popl %ebp\n\t"
        "ret\n\t"
    );
}

__attribute__((force_align_arg_pointer)) uint32_t __cdecl cc_loop_sound(uint32_t first) {
    const std::span<const uint8_t> flags(reinterpret_cast<const uint8_t*>(cc_loop_sound_flags), nl::SoundCount);
    const auto next = vectorSound ? nl::NextSoundVector(flags, first) : nl::NextSoundScalar(flags, first);
    Count(7, next - first);
    return next;
}
// 再生そのもの・ReplayEffectsの抑止フック・フラグの履歴コピー/全消去は元のまま。
__attribute__((naked)) void cc_loop_sound_gate() {
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl %esi\n\t"
        "call _cc_loop_sound\n\t"
        "addl $4,%esp\n\t"
        "movl %eax,4(%esp)\n\t"
        "popal\n\t"
        "popfl\n\t"
        "cmpl $1500,%esi\n\t"
        "jae 1f\n\t"
        "jmp *_cc_loop_sound_body\n\t"
        "1: jmp *_cc_loop_sound_done\n\t"
    );
}

// EAXとXMM0はこの関数では生存値を持たない。16バイトの検索をブリッジ内で完結し、
// 要求が多い場合も要求ごとのC++呼出し・全レジスタ退避を避ける。
__attribute__((naked)) void cc_loop_sound_vector_gate() {
    __asm__ __volatile__(
        "cmpb $0,_cc_loop_trace\n\t"
        "jne _cc_loop_sound_gate\n\t"
        "cmpl $1500,%esi\n\t"
        "jae 4f\n\t"
        "movl _cc_loop_sound_flags,%eax\n\t"
        "cmpb $1,(%eax,%esi)\n\t"
        "je 5f\n\t"
        "1: cmpl $1484,%esi\n\t"
        "ja 3f\n\t"
        "movl _cc_loop_sound_flags,%eax\n\t"
        "movdqu (%eax,%esi),%xmm0\n\t"
        "pcmpeqb 7f,%xmm0\n\t"
        "pmovmskb %xmm0,%eax\n\t"
        "testl %eax,%eax\n\t"
        "jne 2f\n\t"
        "addl $16,%esi\n\t"
        "jmp 1b\n\t"
        "2: bsfl %eax,%eax\n\t"
        "addl %eax,%esi\n\t"
        "jmp *_cc_loop_sound_body\n\t"
        "3: cmpl $1500,%esi\n\t"
        "jae 4f\n\t"
        "movl _cc_loop_sound_flags,%eax\n\t"
        "cmpb $1,(%eax,%esi)\n\t"
        "je 5f\n\t"
        "incl %esi\n\t"
        "jmp 3b\n\t"
        "4: jmp *_cc_loop_sound_done\n\t"
        "5: jmp *_cc_loop_sound_body\n\t"
        ".p2align 4\n\t"
        "7: .byte 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1\n\t"
    );
}

__attribute__((force_align_arg_pointer)) HRESULT __cdecl cc_loop_scene_pair(IDirect3DDevice9* device) {
    Count(1);
    return cccaster::game_interface::DxHook::AdjacentScenePair(device);
}
__attribute__((naked)) void cc_loop_scene_gate() {
    __asm__ __volatile__(
        "movl (%esi),%eax\n\t"
        "movl 4(%eax),%edi\n\t"
        "movl %edi,-4(%ebp)\n\t"
        "pushl %edi\n\t"
        "call _cc_loop_scene_pair\n\t"
        "addl $4,%esp\n\t"
        "jmp *_cc_loop_scene_done\n\t"
    );
}
}

namespace cccaster::game_memory::native_loops {
void Install() {
    static bool attempted = false;
    if (attempted) return;
    attempted = true;
    cc_loop_trace = std::getenv("CCCASTER_NATIVE_LOOP_TRACE") != nullptr;
    if (std::getenv("CCCASTER_DISABLE_NATIVE_LOOPS")) {
        domain::session::DebugLog("[NativeLoops] disabled=1"); return;
    }
    if (!game_build::RuntimeValidated() || !GameRuntime::IsSteam()) return;
    for (const auto& range : signatures::Ranges) {
        if (!steam_code::Matches(range)) {
            domain::session::DebugLog("[NativeLoops] signature mismatch address=%08X; unchanged", unsigned(range.address));
            return;
        }
    }
    collisionValidated = true;
    cc_loop_pool = GameRuntime::Preferred(0x6e28f0);
    cc_loop_collision_body = GameRuntime::Preferred(0x4c7455);
    cc_loop_collision_done = GameRuntime::Preferred(0x4c7516);
    cc_loop_end_a = GameRuntime::Preferred(0x7accca);
    cc_loop_end_b = GameRuntime::Preferred(0x7ace1c);
    cc_loop_end_c = GameRuntime::Preferred(0x7acb54);
    cc_loop_end_d = GameRuntime::Preferred(0x7acb54);
    cc_loop_body_a = GameRuntime::Preferred(0x4aaa29);
    cc_loop_body_b = GameRuntime::Preferred(0x4aaa8c);
    cc_loop_body_c = GameRuntime::Preferred(0x4aaafc);
    cc_loop_body_d = GameRuntime::Preferred(0x4aab3c);
    cc_loop_done_a = GameRuntime::Preferred(0x4aaa7e);
    cc_loop_done_b = GameRuntime::Preferred(0x4aaadd);
    cc_loop_done_c = GameRuntime::Preferred(0x4aab27);
    cc_loop_done_d = GameRuntime::Preferred(0x4aab5a);
    cc_loop_sound_flags = GameRuntime::Preferred(0x7d47b0);
    cc_loop_sound_body = GameRuntime::Preferred(0x52195f);
    cc_loop_sound_done = GameRuntime::Preferred(0x521979);
    cc_loop_scene_done = GameRuntime::Preferred(0x50f985);
    vectorSound = IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE) != FALSE;
    const std::array<uintptr_t, 8> targets{
        uintptr_t(&cc_loop_collision_gate), uintptr_t(&cc_loop_scene_gate),
        uintptr_t(&cc_loop_live_a_gate), uintptr_t(&cc_loop_live_b_gate),
        uintptr_t(&cc_loop_live_c_gate), uintptr_t(&cc_loop_live_d_gate),
        uintptr_t(&cc_loop_commands_gate), vectorSound ? uintptr_t(&cc_loop_sound_vector_gate) : uintptr_t(&cc_loop_sound_gate)};
    std::array<std::vector<uint8_t>, 8> original, replacement;
    std::vector<patch::Spec> specs;
    // ScenePairMergeが命令照合できた場合だけ、既存の統合を呼出し元側へまとめる。
    const bool scene = game_interface::scene_pair_merge::endCaller == GameRuntime::Preferred(0x50f97c);
    // 試験専用の5群選択。未指定は従来どおり全群。速度比較で役割を交代できる。
    unsigned mask = 31;
    if (const char* value = std::getenv("CCCASTER_TEST_NATIVE_MASK")) {
        char* end = nullptr;
        const auto parsed = std::strtoul(value, &end, 10);
        if (end != value && *end == '\0' && parsed <= 31) mask = unsigned(parsed);
    }
    constexpr unsigned bits[]{1,2,4,4,4,4,8,16};
    for (size_t i = 0; i < targets.size(); ++i) {
        if (!(mask & bits[i])) continue;
        if (i == 1 && !scene) continue;
        const auto& site = signatures::Sites[i];
        const auto address=GameRuntime::Preferred(site.address,site.original.size());
        original[i].assign(reinterpret_cast<const uint8_t*>(address),reinterpret_cast<const uint8_t*>(address)+site.original.size());
        replacement[i].assign(site.original.size(), 0x90);
        replacement[i][0] = 0xe9;
        const auto relative = uint32_t(targets[i] - (address + 5));
        std::memcpy(replacement[i].data() + 1, &relative, 4);
        specs.push_back({site.name, address, original[i], replacement[i]});
    }
    const auto result = patch::Apply(specs);
    domain::session::DebugLog("[NativeLoops] enabled=%u scene=%u sse2=%u patch=%s error=%s rollbackFailed=%u",
        unsigned(bool(result)), unsigned(scene), unsigned(vectorSound), result.name,
        patch::Name(result.error), unsigned(result.rollbackFailed));
    if (result.rollbackFailed) ExitProcess(ERROR_WRITE_FAULT);
    domain::session::DebugLog("[NativeLoopMask] requested=%u applied=%u", mask, result ? mask & (scene ? 31u : 29u) : 0u);
}
void Trace() {
    Verify();
    if (!cc_loop_trace || ++frames % 300 != 0) return;
    for (unsigned i = 0; i < calls.size(); ++i)
        domain::session::DebugLog("[NativeLoopCount] group=%u calls=%llu skipped=%llu", i,
            static_cast<unsigned long long>(calls[i]), static_cast<unsigned long long>(skipped[i]));
}
}
