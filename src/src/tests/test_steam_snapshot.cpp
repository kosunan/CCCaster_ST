#include "core_dll/rollback/SteamSnapshotBuilder.hpp"
#include "core_dll/rollback/GameSnapshotLayout.hpp"
#include "shared_contracts/GameLoadedCode.hpp"
#include "test_support.hpp"
#include <array>
#include <fstream>
#include <map>
#include <numeric>
#include <cstdio>

using namespace cccaster::sync;
using cccaster::game_build::LoadedImage;
namespace pointers = cccaster::sync::steam_pointers;

static std::vector<SteamSnapshotFragment> Fragments() {
    std::vector<SteamSnapshotFragment> result;
    const auto append = [&](const auto &table) { result.insert(result.end(), std::begin(table), std::end(table)); };
    append(SteamSnapshotFragments);
    append(SteamRoundCallAdditionalFragments);
    append(SteamDisplayFragments);
    append(SteamMiscFragments);
    append(SteamIntroFragments);
    return result;
}

static void Coverage() {
    CC_CASE("vB最新66ルートと既存Steam追加6ルートの全byteを一度だけ対応づける");
    std::map<unsigned, SnapshotNode> roots;
    for (unsigned i = 0; i < std::size(GameSnapshotLayout); ++i)
        if (GameSnapshotLayout[i].parent == -1) roots[i >= 3061 ? i + 6 : i] = GameSnapshotLayout[i];
    CC_CHECK_EQ(roots.size(), 66);
    // 旧RealGameMemoryで表外に追加していた6領域。新表から期待値を作らない。
    roots[3061] = {-1, 0x557d2a, 0, 1};
    roots[3062] = {-1, 0x563574, 0, 2};
    roots[3063] = {-1, 0x562a70, 0, 4};
    roots[3064] = {-1, 0x55dec0, 0, 4};
    roots[3065] = {-1, 0x55def0, 0, 4};
    roots[3066] = {-1, 0x55df24, 0, 4};
    roots[3072] = {-1, 0x563868, 0, 224}; // Steam演出RNG、479BB0のindex=1と55語
    roots[3073] = {-1, 0x54cfe4, 0, 4}; // RoundCall初期化分岐の入力
    std::map<unsigned, std::vector<unsigned char>> coverage;
    for (const auto &[index, root] : roots) coverage[index].resize(root.size);
    const auto fragments = Fragments();
    size_t bytes = 0;
    for (const auto &f : fragments) {
        const auto found = roots.find(f.legacyNode);
        CC_CHECK(found != roots.end());
        if (found == roots.end()) continue;
        const auto &root = found->second;
        const bool contained = f.size && f.legacyVa >= root.source &&
            uint64_t(f.legacyVa) + f.size <= uint64_t(root.source) + root.size;
        CC_CHECK(contained);
        if (!contained) continue;
        auto &hits = coverage[f.legacyNode];
        for (size_t b = f.legacyVa - root.source; b < f.legacyVa - root.source + f.size; ++b) ++hits[b];
        bytes += f.size;
        CC_CHECK(f.steamRva != 0 && uint64_t(f.steamRva) + f.size <= 0xf3e000);
        CC_CHECK(f.evidenceInstructionRva >= 0x1000 && f.evidenceInstructionRva < 0x16a5eb);
    }
    for (const auto &[index, hits] : coverage) {
        const auto missing = std::count(hits.begin(), hits.end(), 0);
        const auto duplicate = std::count_if(hits.begin(), hits.end(), [](auto n) { return n > 1; });
        if (missing || duplicate) std::printf("legacyNode=%u missing=%u duplicate=%u\n", index, unsigned(missing), unsigned(duplicate));
        CC_CHECK_EQ(missing, 0);
        CC_CHECK_EQ(duplicate, 0);
    }
    for (size_t i = 0; i < fragments.size(); ++i)
        for (size_t j = i + 1; j < fragments.size(); ++j) {
            const auto &a = fragments[i], &b = fragments[j];
            CC_CHECK(uint64_t(a.steamRva) + a.size <= b.steamRva || uint64_t(b.steamRva) + b.size <= a.steamRva);
        }
    CC_CHECK_EQ(bytes, 1229542);
    std::printf("%u fragments / 74 roots / %u root bytes\n", unsigned(fragments.size()), unsigned(bytes));
}

#if defined(_WIN32) && defined(__i386__)
static void Put32(void *at, uint32_t value) { std::memcpy(at, &value, sizeof(value)); }

static void Roundtrip() {
    CC_CASE("本物BuilderとPointerSnapshotで異なる4配置・3000子nodeを保存復元する");
    constexpr size_t imageSize = 0xf3e000, resourceSize = 1000 * 0x80;
    std::array<void *, 4> allocations{};
    std::vector<SnapshotNode> nodes{{-1, 1, 0, 1}};
    pointers::lifetimeGuardInstalled = false;
    CC_CHECK(!BuildSteamSnapshotNodes({0x400000, imageSize}, nodes));
    CC_CHECK(nodes.empty());
    // hookを実装・実行する試験ではない。監視設置済みというBuilderの前提だけを模擬する。
    pointers::lifetimeGuardInstalled = true;
    pointers::cccaster_steam_resource_generation = 1;
    pointers::cccaster_steam_resource_exhausted = 0;
    const auto generation = pointers::CaptureGeneration();
    CC_CHECK(pointers::ValidateGeneration(generation));
    CC_CHECK(!pointers::ValidateGeneration(0));
    InterlockedIncrement(&pointers::cccaster_steam_resource_generation);
    CC_CHECK(!pointers::ValidateGeneration(generation));
    pointers::cccaster_steam_resource_exhausted = 1;
    CC_CHECK_EQ(pointers::CaptureGeneration(), 0);
    pointers::cccaster_steam_resource_exhausted = 0;
    CC_CHECK(!BuildSteamSnapshotNodes({0x400000, imageSize - 1}, nodes));
    CC_CHECK(nodes.empty());
    CC_CHECK(!BuildSteamSnapshotNodes({UINTPTR_MAX - 0x1000, imageSize}, nodes));
    CC_CHECK(nodes.empty());
    for (size_t trial = 0; trial < allocations.size(); ++trial) {
        auto *memory = static_cast<uint8_t *>(VirtualAlloc(nullptr, imageSize + resourceSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        allocations[trial] = memory;
        CC_CHECK(memory != nullptr);
        if (!memory) continue;
        const auto base = reinterpret_cast<uintptr_t>(memory);
        for (size_t previous = 0; previous < trial; ++previous) CC_CHECK(memory != allocations[previous]);
        CC_CHECK(BuildSteamSnapshotNodes({base, imageSize}, nodes));
        const auto roots = Fragments().size();
        CC_CHECK_EQ(nodes.size(), roots + 3000);
        if (nodes.size() != roots + 3000) continue;
        int objectRoot = -1;
        for (size_t i = 0; i < roots; ++i) {
            CC_CHECK_EQ(nodes[i].parent, -1);
            CC_CHECK(nodes[i].source >= base && nodes[i].source + nodes[i].size <= base + imageSize);
            auto *data = reinterpret_cast<uint8_t *>(nodes[i].source);
            for (size_t b = 0; b < nodes[i].size; ++b) data[b] = uint8_t(b * 13 + i + trial);
            if (nodes[i].source == base + 0x2e28f0) objectRoot = int(i);
        }
        CC_CHECK(objectRoot >= 0);
        if (objectRoot < 0) continue;
        for (size_t object = 0; object < 1000; ++object) {
            const size_t child = roots + object * 3;
            CC_CHECK_EQ(nodes[child].parent, objectRoot);
            CC_CHECK_EQ(nodes[child].source, 0x320 + 0x33c * object);
            CC_CHECK_EQ(nodes[child].offset, 0x38);
            CC_CHECK_EQ(nodes[child + 1].parent, child);
            CC_CHECK_EQ(nodes[child + 2].parent, child + 1);
            CC_CHECK_EQ(nodes[child + 1].offset, 0);
            CC_CHECK_EQ(nodes[child + 2].offset, 0);
            for (size_t n = child; n < child + 3; ++n) CC_CHECK_EQ(nodes[n].size, 4);
            auto *frame = memory + imageSize + object * 0x80;
            Put32(memory + 0x2e28f0 + 0x320 + object * 0x33c, object % 4 == 0 ? 0 : uint32_t(uintptr_t(frame)));
            Put32(frame + 0x38, object % 4 == 1 ? 0 : uint32_t(uintptr_t(frame + 0x40)));
            Put32(frame + 0x40, object % 4 == 2 ? 0 : uint32_t(uintptr_t(frame + 0x60)));
            Put32(frame + 0x60, 0xffffffff); // 第3段は値。4段目としてdereferenceしてはいけない。
        }
        PointerSnapshot snapshot;
        CC_CHECK(snapshot.Configure(nodes, true));
        CC_CHECK_EQ(snapshot.Size(), 1241542);
        std::vector<char> saved(snapshot.Size()), restored(snapshot.Size());
        CC_CHECK(!snapshot.Save(std::span<char>(saved).first(saved.size() - 1)));
        CC_CHECK(snapshot.Save(saved));
        for (size_t i = 0; i < roots; ++i) std::memset(reinterpret_cast<void *>(nodes[i].source), 0xcc, nodes[i].size);
        for (size_t object = 0; object < 1000; ++object) {
            // 保存blobに親slotがあれば親から順に修復できる。無効だった枝の所有領域には触れない。
            auto *frame = memory + imageSize + object * 0x80;
            if (object % 4 != 0) Put32(frame + 0x38, 0xdeadbeef);
            if (object % 4 >= 2) Put32(frame + 0x40, 0xdeadbeef);
            if (object % 4 == 3) Put32(frame + 0x60, 0x12345678);
        }
        CC_CHECK(snapshot.Load(saved));
        CC_CHECK(snapshot.Save(restored));
        CC_CHECK(saved == restored);
        std::printf("base=0x%08x nodes=%u bytes=%u roundtrip=equal\n", unsigned(base), unsigned(nodes.size()), unsigned(snapshot.Size()));
    }
    for (auto *memory : allocations) if (memory) CC_CHECK(VirtualFree(memory, 0, MEM_RELEASE) != 0);
    pointers::lifetimeGuardInstalled = false;
}
#endif

// 実EXEから各根拠命令の直近32byteに対応VAがあることを検査する。
// 用途・長さの妥当性を証明する逆アセンブルの代用ではない。
static bool IndexedEvidence(const std::vector<uint8_t> &file, const SteamSnapshotFragment &f) {
    using namespace cccaster::game_build;
    const auto valueAt = [&](uint32_t rva) {
        size_t offset{};
        return FileOffset(file, rva, 4, offset) ? U32(file, offset) : 0u;
    };
    // レジスター初期値と命令の変位/strideから住所を作るケース。
    // 同定した命令位置そのものを保持し、直接operand検査から単に除外しない。
    if (f.legacyVa == 0x557db8)
        return f.evidenceInstructionRva == 0xbd5d8 && valueAt(0xbd574) == 0x400000 + f.steamRva &&
            valueAt(0xbd5da) == 0x20c && f.size == 2 * 0x20c;
    if (f.legacyVa == 0x558608)
        return f.evidenceInstructionRva == 0x73790 && valueAt(0x73737) - 0x300 == 0x400000 + f.steamRva &&
            valueAt(0x73792) == 0xffffff00 && valueAt(0x737a2) == 0xfffffe00;
    if (f.legacyVa == 0x563580 || f.legacyVa == 0x5635f4) {
        const bool second = f.legacyVa == 0x5635f4;
        return f.evidenceInstructionRva == (second ? 0xcedf5u : 0xcedd2u) &&
            valueAt(0xcedc8) - 0x70 + (second ? 0x74 : 0) == 0x400000 + f.steamRva &&
            (valueAt(0xcedf5) & 0xffffff) == 0x74c683;
    }
    if (f.legacyVa == 0x564070)
        return f.evidenceInstructionRva == 0x799b3 && valueAt(0x799b6) + 4 == 0x400000 + f.steamRva;
    if (f.legacyVa == 0x5641a4 || f.legacyVa == 0x564200) {
        const bool second = f.legacyVa == 0x564200;
        return f.evidenceInstructionRva == (second ? 0x7c38cu : 0x7c235u) &&
            valueAt(0x7c197) + 0x44 + (second ? 0x5c : 0) == 0x400000 + f.steamRva &&
            (valueAt(0x7c38c) & 0xffffff) == 0x5cc183;
    }
    return false;
}

static void Evidence(const char *path) {
    CC_CASE("実Steam EXEの版と保存断片の根拠アドレスoperandを確認する");
    using namespace cccaster::game_build;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    CC_CHECK(bool(input));
    if (!input) return;
    std::vector<uint8_t> file(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(file.data()), file.size());
    CC_CHECK(IdentifyFile(file) == Edition::Steam20170105);
    for (const auto &f : Fragments()) {
        size_t offset{};
        const bool located = FileOffset(file, f.evidenceInstructionRva, 32, offset);
        CC_CHECK(located);
        if (!located) continue;
        bool operand = IndexedEvidence(file, f);
        for (size_t i = 0; i + 4 <= 32; ++i) {
            const auto value = U32(file, offset + i);
            if (value >= 0x400000 + f.steamRva && value < 0x400000 + f.steamRva + f.size) operand = true;
        }
        if (!operand) std::printf("operand not found node=%u legacy=%08x rva=%08x evidence=%08x\n", f.legacyNode, f.legacyVa, f.steamRva, f.evidenceInstructionRva);
        CC_CHECK(operand);
    }
}

int main(int argc, char **argv) {
    Coverage();
#if defined(_WIN32) && defined(__i386__)
    Roundtrip();
#else
    std::puts("32bit Windowsでないため実メモリー配置・保存復元試験は未実施");
#endif
    for (int i = 1; i < argc; ++i) Evidence(argv[i]);
    if (argc == 1) std::puts("実EXE未指定のため命令operand照合は未実施");
    return cccaster::test::Summarize("steam_snapshot");
}
