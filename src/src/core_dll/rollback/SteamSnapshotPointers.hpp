#pragma once
#include "PointerSnapshot.hpp"
#include <array>
#include <cstdint>
#if defined(_WIN32) && defined(__i386__)
#include "SteamReplayEffects.hpp"
#endif

namespace cccaster::sync::steam_pointers {
// 静的に同定した子3段。ASLR済みのポインター値にRVA差分を足してはいけない。
// 第1段は frame+38 のポインターslot、第2段は補助構造の先頭slot、
// 第3段はASV0データ先頭DWORD（ポインターではない）。
inline constexpr uint32_t ObjectStride = 0x33c;
inline constexpr uint32_t ObjectCount = 1000;
inline constexpr uint32_t ObjectPointerOffset = 0x320;
inline constexpr uint32_t FrameAuxiliaryOffset = 0x38;
inline constexpr uint32_t AuxiliaryBytes = 0x1c;
inline constexpr uint32_t Asv0Bytes = 0x0a;
inline constexpr bool StructureVerified = true;
// 資源破棄/再ロードが全rollback窓の外であるという静的仮定は採用しない。
// 実稼働では下記の破棄・parser監視世代を保存blobに含め、復元前に検査する。
inline constexpr bool LifetimeVerified = false;
// 旧来の無監視利用は引き続き禁止。AppendVerifiedNodesは実行時guardを要求する。
inline constexpr bool CanActivate = StructureVerified && LifetimeVerified;

// 署名に絶対アドレスoperandはない。版全体照合済みのSteam EXEに限定する。
inline constexpr uint32_t ParseRva = 0x304e0;
inline constexpr uint32_t DestroyFrameRva = 0x33570;
inline constexpr uint32_t DestroyCollectionRva = 0x33950;
inline constexpr std::array<uint8_t, 13> ParseCode =
    {0x55,0x8b,0xec,0x83,0xec,0x54,0x53,0x56,0x57,0x8b,0xda,0x8b,0xf9};
inline constexpr std::array<uint8_t, 14> DestroyFrameCode =
    {0x56,0x8b,0xf1,0x85,0xf6,0x0f,0x84,0x12,0x02,0x00,0x00,0x83,0xfa,0x01};
inline constexpr std::array<uint8_t, 15> DestroyCollectionCode =
    {0x55,0x8b,0xec,0x51,0x56,0x8b,0xf1,0x85,0xf6,0x0f,0x84,0x41,0x01,0x00,0x00};

#if defined(_WIN32) && defined(__i386__)
// asmが参照するC symbolを保持。ゲーム側のECX/EDX/stack引数を一切触らない。
extern "C" {
__attribute__((used)) inline volatile LONG cccaster_steam_resource_generation = 1;
__attribute__((used)) inline volatile LONG cccaster_steam_resource_exhausted = 0;
__attribute__((used)) inline void *cccaster_steam_resource_parse_original = nullptr;
__attribute__((used)) inline void *cccaster_steam_resource_frame_original = nullptr;
__attribute__((used)) inline void *cccaster_steam_resource_collection_original = nullptr;
#define CCCASTER_RESOURCE_HOOK(Name, Original) \
    __attribute__((naked, used)) inline void Name() { \
        __asm__ __volatile__("pushfl\n\t" \
            "lock incl _cccaster_steam_resource_generation\n\t" \
            "jnz 1f\n\t" \
            "movl $1, _cccaster_steam_resource_exhausted\n\t" \
            "1: popfl\n\t" \
            "jmp *_" #Original "\n\t"); \
    }
CCCASTER_RESOURCE_HOOK(cccaster_steam_resource_parse_hook, cccaster_steam_resource_parse_original)
CCCASTER_RESOURCE_HOOK(cccaster_steam_resource_frame_hook, cccaster_steam_resource_frame_original)
CCCASTER_RESOURCE_HOOK(cccaster_steam_resource_collection_hook, cccaster_steam_resource_collection_original)
#undef CCCASTER_RESOURCE_HOOK
}
inline bool lifetimeGuardInstalled = false;
inline uintptr_t lifetimeGuardBase = 0;

// 初期化用。全ゲームコード照合後、資源loader/ゲーム更新が動く前に呼ぶ。
// Save/Load中に資源を破棄しないゲームスレッド所有条件は呼出側の責任。
inline bool InstallLifetimeGuard(game_build::Edition edition, const game_build::LoadedImage &image) {
    if (edition != game_build::Edition::Steam20170105) return false;
    if (lifetimeGuardInstalled) return lifetimeGuardBase == image.base;
    if (!steam_effects::Match(image, ParseRva, ParseCode, std::array<size_t, 0>{}) ||
        !steam_effects::Match(image, DestroyFrameRva, DestroyFrameCode, std::array<size_t, 0>{}) ||
        !steam_effects::Match(image, DestroyCollectionRva, DestroyCollectionCode, std::array<size_t, 0>{}))
        return false;
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    const std::array<void *, 3> targets = {
        reinterpret_cast<void *>(image.Resolve(ParseRva, ParseCode.size())),
        reinterpret_cast<void *>(image.Resolve(DestroyFrameRva, DestroyFrameCode.size())),
        reinterpret_cast<void *>(image.Resolve(DestroyCollectionRva, DestroyCollectionCode.size()))};
    const std::array<void *, 3> hooks = {
        reinterpret_cast<void *>(cccaster_steam_resource_parse_hook),
        reinterpret_cast<void *>(cccaster_steam_resource_frame_hook),
        reinterpret_cast<void *>(cccaster_steam_resource_collection_hook)};
    const std::array<void **, 3> originals = {
        &cccaster_steam_resource_parse_original, &cccaster_steam_resource_frame_original,
        &cccaster_steam_resource_collection_original};
    size_t created = 0;
    for (; created < targets.size(); ++created)
        if (MH_CreateHook(targets[created], hooks[created], originals[created]) != MH_OK) break;
    bool ok = created == targets.size();
    if (ok) for (const auto target : targets)
        if (MH_EnableHook(target) != MH_OK) { ok = false; break; }
    if (!ok) {
        for (size_t i = 0; i < created; ++i) {
            MH_DisableHook(targets[i]);
            if (MH_RemoveHook(targets[i]) == MH_OK) *originals[i] = nullptr;
        }
        return false;
    }
    lifetimeGuardBase = image.base;
    lifetimeGuardInstalled = true;
    return true;
}
inline uint32_t CaptureGeneration() {
    if (!lifetimeGuardInstalled ||
        InterlockedCompareExchange(&cccaster_steam_resource_exhausted, 0, 0)) return 0;
    return static_cast<uint32_t>(InterlockedCompareExchange(&cccaster_steam_resource_generation, 0, 0));
}
#else
inline uint32_t CaptureGeneration() { return 0; }
#endif
inline bool ValidateGeneration(uint32_t saved) {
    return saved != 0 && saved == CaptureGeneration();
}

// 子表。実稼働への追加には監視hook設置とblob世代検査が必須。
// rootIndexはobject pool全体のnode、firstChildIndexはこの3000nodeの開始位置。
constexpr std::array<SnapshotNode, ObjectCount * 3>
CandidateNodes(int rootIndex, int firstChildIndex) {
    std::array<SnapshotNode, ObjectCount * 3> result{};
    for (uint32_t object = 0; object < ObjectCount; ++object) {
        const auto at = object * 3;
        result[at] = {rootIndex, ObjectPointerOffset + ObjectStride * object,
                      FrameAuxiliaryOffset, 4};
        result[at + 1] = {firstChildIndex + int(at), 0, 0, 4};
        result[at + 2] = {firstChildIndex + int(at + 1), 0, 0, 4};
    }
    return result;
}

inline bool AppendVerifiedNodes(std::vector<SnapshotNode> &nodes, int rootIndex) {
    if (!StructureVerified || !CaptureGeneration() || rootIndex < 0 || size_t(rootIndex) >= nodes.size() ||
        nodes[rootIndex].size != ObjectStride * ObjectCount) return false;
    const auto children = CandidateNodes(rootIndex, int(nodes.size()));
    nodes.insert(nodes.end(), children.begin(), children.end());
    return true;
}
static_assert(ObjectPointerOffset + ObjectStride * (ObjectCount - 1) + 4 <=
              ObjectStride * ObjectCount);
static_assert(CandidateNodes(36, 37)[2999].parent == 3035);
} // namespace cccaster::sync::steam_pointers
