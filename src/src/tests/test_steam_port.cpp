#include "shared_contracts/GameLoadedCode.hpp"
#include "test_support.hpp"
#include "core_dll/mbaa_mem/SteamAddressMap.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#ifdef _WIN32
#include "core_dll/mbaa_mem/SteamInputPatch.hpp"
#include "core_dll/mbaa_mem/SteamMenuPatch.hpp"
#include "core_dll/rollback/SteamReplayEffects.hpp"
#endif

using namespace cccaster::game_build;
using Bytes = std::vector<uint8_t>;
static void Put16(Bytes& b, size_t p, uint16_t v) { b.at(p) = v; b.at(p + 1) = v >> 8; }
static void Put32(Bytes& b, size_t p, uint32_t v) { Put16(b,p,v); Put16(b,p+2,v>>16); }

// テスト用PEローダー: 全セクションを仮想imageへ配置してからrelocを適用する。
// 被検関数の.text生成・FileOffsetは使用しない。ゲームコードは実行しない。
static Bytes MapImage(const Bytes& file, uint32_t base) {
    const size_t nt = U32(file, 0x3c), opt = nt + 24;
    Bytes image(U32(file, opt + 56));
    const size_t headers = U32(file, opt + 60);
    std::copy_n(file.begin(), headers, image.begin());
    const size_t table = opt + U16(file, nt + 20);
    for (unsigned n = 0; n < U16(file, nt + 6); ++n) {
        const size_t s = table + n * 40;
        const size_t rva = U32(file, s + 12), raw = U32(file, s + 20), count = U32(file, s + 16);
        if (raw + count > file.size() || rva + count > image.size()) throw std::runtime_error("section bounds");
        std::copy_n(file.begin() + raw, count, image.begin() + rva);
    }
    const uint32_t delta = base - U32(file, opt + 28);
    size_t cursor = U32(file, opt + 136), end = cursor + U32(file, opt + 140);
    while (cursor < end) {
        const size_t page = U32(image, cursor), block = U32(image, cursor + 4);
        if (block < 8 || cursor + block > end) throw std::runtime_error("reloc bounds");
        for (size_t p = cursor + 8; p < cursor + block; p += 2) {
            const uint16_t entry = U16(image, p);
            if (!entry) continue;
            if ((entry >> 12) != 3) throw std::runtime_error("reloc type");
            const size_t at = page + (entry & 0xfff);
            Put32(image, at, U32(image, at) + delta);
        }
        cursor += block;
    }
    return image;
}

#ifdef _WIN32
static void Signatures(const Bytes& file, const PeIdentity& p) {
    CC_CASE("Steam局所署名を再配置済みの独立バッファで照合する");
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, p.imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CC_CHECK(memory != nullptr);
    if (!memory) return;
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory));
    const auto mapped = MapImage(file, base);
    std::memcpy(memory, mapped.data(), mapped.size());
    LoadedImage image{base, p.imageSize};
    CC_CASE("Steam選択確定フックと選択データ参照を再配置後も照合する");
    const unsigned char stageExpected[] = {0x83,0xf8,0xff,0x74,0x10};
    CC_CHECK(std::memcmp(memory + 0x7ffed, stageExpected, sizeof(stageExpected)) == 0);
    const unsigned char randomExpected[] = {0x55,0x8b,0xec,0x83,0xec,0x08};
    CC_CHECK(std::memcmp(memory + 0x86260, randomExpected, sizeof(randomExpected)) == 0);
    CC_CHECK_EQ(mbaa::steam::DataPreferredVa(0x74d8fc), 0x7b4434u);
    CC_CHECK_EQ(mbaa::steam::DataPreferredVa(0x74d920), 0x7b4458u);
    CC_CHECK_EQ(mbaa::steam::DataPreferredVa(0x74fd98), 0x7b6228u);
    CC_CHECK_EQ(mbaa::steam::DataPreferredVa(0x74d808), 0x7b433cu);
    CC_CASE("Steam 1.4の起動・再戦・描画の実命令を検証する");
    struct Signature { unsigned rva; std::vector<uint8_t> bytes; };
    const Signature checks[]{
        {0xf0cf0, {0x55,0x8b,0xec,0x81,0x7d,0x0c,0x10,1,0,0,0x75,8,0x8b,0x4d,8,0xe8,0x9c,2,0,0,0x33,0xc0,0x5d,0xc3}},
        {0x945a7, {0x83,0x79,0x44,0x1e}},
        {0x81841, {0xc7,0,0x1e,0,0,0}},
        {0x81c58, {0xc7,0x44,0xdf,0x14,0x1e,0,0,0}},
        {0x82593, {0x83,0x7e,0x34,0x1e}},
        {0xe5690, {0x8d,0x3c,2,0xc1,0xff,2}},
        {0x6e9b0, {0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x1c}},
    };
    for (const auto& check : checks)
        CC_CHECK(std::memcmp(memory + check.rva, check.bytes.data(), check.bytes.size()) == 0);
    CC_CHECK_EQ(U32(mapped,0x8aaa5),base+0x16b318); // GetClientRect IAT
    CC_CHECK_EQ(U32(mapped,0x1215b8),base+0x3cf228); // BGM table loop position
    namespace input = cccaster::game_memory::steam_input;
    namespace menu = cccaster::game_memory::steam_menu;
    namespace effects = cccaster::sync::steam_effects;
    for (auto rva : input::PatchRvas) {
        CC_CHECK(std::memcmp(memory + rva, input::Original.data(), input::Original.size()) == 0);
        CC_CHECK_EQ(U32(mapped, rva - 4), base + 0x3d4df4);
    }
    CC_CHECK(menu::MatchesStartup(image));
    memory[menu::DispatchPatchRva + 3] ^= 1;
    CC_CHECK(!menu::MatchesStartup(image));
    memory[menu::DispatchPatchRva + 3] ^= 1;
    // Sound署名の保護属性確認も通す。EXEデータは一切呼び出さない。
    DWORD old{};
    CC_CHECK(VirtualProtect(memory, p.imageSize, PAGE_EXECUTE_READ, &old) != 0);
    effects::Sites sites;
    CC_CHECK(effects::Resolve(Edition::Steam20170105, image, sites));
    CC_CHECK_EQ(reinterpret_cast<uintptr_t>(sites.dispatch), base + 0x12195f);
    CC_CHECK_EQ(sites.skip, base + 0x121970);
    CC_CHECK(effects::ResolveRngTrace(Edition::Steam20170105, image) != nullptr);
    CC_CHECK(!effects::Resolve(Edition::Carnival140, image, sites));
    CC_CHECK(VirtualProtect(memory, p.imageSize, PAGE_READWRITE, &old) != 0);
    memory[0x121950] ^= 1;
    CC_CHECK(VirtualProtect(memory, p.imageSize, PAGE_EXECUTE_READ, &old) != 0);
    CC_CHECK(!effects::Resolve(Edition::Steam20170105, image, sites));
    CC_CHECK(VirtualFree(memory, 0, MEM_RELEASE) != 0);
}
#endif

static void CheckFile(const char* path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    CC_CHECK(bool(stream));
    if (!stream) return;
    Bytes file(static_cast<size_t>(stream.tellg()));
    stream.seekg(0); stream.read(reinterpret_cast<char*>(file.data()), file.size());
    PeIdentity p;
    CC_CHECK(ReadHeaders(file, p));
    const auto edition = IdentifyFile(file);
    CC_CHECK(edition != Edition::Unknown);
    if (edition == Edition::Unknown) return;
    std::cout << path << ": " << Name(edition) << '\n';
    const size_t opt = U32(file, 0x3c) + 24;
    const bool hasRelocations = U32(file, opt + 140) != 0;
    CC_CASE("実EXEの再配置・入口ロック・改変を識別する");
    for (const uint32_t base : {0x400000u, 0x320000u, 0x10000000u, 0x70000000u}) {
        auto image = MapImage(file, base);
        auto text = std::span(image).subspan(p.textRva, p.textSize);
        const bool supportedBase = base == p.imageBase || hasRelocations;
        CC_CHECK(ValidateLoadedCode(file, text, base) == supportedBase);
        CC_CHECK(!ValidateLoadedCode(file, text, 0));
        CC_CHECK(!ValidateLoadedCode(file, text, 0xffff0000));
        CC_CHECK(!ValidateLoadedCode(file, text.first(text.size()-1), base));
        CC_CHECK(!ValidateLoadedCode(file, text, base + 0x10000));
        image[p.entryRva] = 0xeb; image[p.entryRva + 1] = 0xfe;
        CC_CHECK(ValidateLoadedCode(file, text, base) == supportedBase);
        CC_CHECK(!ValidateLoadedCode(file, text, base, false));
        image[p.entryRva + 1] = 0xfd;
        CC_CHECK(!ValidateLoadedCode(file, text, base));
        image[p.entryRva + 1] = 0xfe;
        image[p.textRva + p.textSize / 2] ^= 1;
        CC_CHECK(!ValidateLoadedCode(file, text, base));
    }
    CC_CASE("不正relocの型・長さ・範囲と改変ファイルを拒否する");
    const auto mapped = MapImage(file, 0x400000);
    const auto text = std::span(mapped).subspan(p.textRva, p.textSize);
    size_t reloc{};
    CC_CHECK(FileOffset(file, U32(file, opt+136), U32(file, opt+140), reloc) == hasRelocations);
    if (U32(file, opt+140)) {
        auto bad = file; Put32(bad, reloc+4, 7); CC_CHECK(!ValidateLoadedCode(bad, text, 0x400000));
        bad = file; Put32(bad, reloc+4, 0xfffffff0); CC_CHECK(!ValidateLoadedCode(bad, text, 0x400000));
        bad = file; Put16(bad, reloc+8, 0xa000); CC_CHECK(!ValidateLoadedCode(bad, text, 0x400000));
        bad = file; Put32(bad, reloc, p.imageSize); Put16(bad, reloc+8, 0x3000);
        CC_CHECK(!ValidateLoadedCode(bad, text, 0x400000));
        bad = file; Put32(bad, opt+140, U32(file,opt+140)-1); CC_CHECK(!ValidateLoadedCode(bad, text, 0x400000));
        bad = file; Put32(bad, opt+136, 0xfffffff0); CC_CHECK(!ValidateLoadedCode(bad, text, 0x400000));
    }
    auto bad = file; bad[p.textOffset] ^= 1; CC_CHECK(!ValidateLoadedCode(bad, text, 0x400000));
    bad = file; Put32(bad,opt+136,0); Put32(bad,opt+140,0);
    CC_CHECK(!ValidateLoadedCode(bad, text, 0x320000));
#ifdef _WIN32
    if (edition == Edition::Steam20170105) Signatures(file,p);
#endif
}

int main(int argc, char** argv) {
    CC_CASE("空のファイル・コードを拒否する");
    CC_CHECK(!ValidateLoadedCode({}, {}, 0x400000));
    size_t offset{};
    CC_CHECK(!FileOffset({}, 0, 4, offset));
    for (int i=1; i<argc; ++i) CheckFile(argv[i]);
    if (argc == 1) std::cout << "実EXE未指定: 再配置と局所署名の検証は未実施\n";
    return cccaster::test::Summarize("steam_port");
}
