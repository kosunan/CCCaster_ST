#pragma once

#include <cstdint>
#include "core_dll/rollback/SteamSnapshotLayout.hpp"
#include "core_dll/rollback/SteamRoundCallFragments.hpp"
#include "core_dll/rollback/SteamDisplayFragments.hpp"
#include "core_dll/rollback/SteamMiscFragments.hpp"

namespace mbaa::steam {

// Steam 2017-01-05 のデータだけを対象にする。戻り値は実アドレスでなく
// preferred VA (ImageBase=0x400000)。利用側で実ロード先+RVAへ変換する。
// 根拠: docs/design/steam_04_addresses.md。未解決・コードアドレスは0。
// 一定差分による全域変換や、未知値を旧版へ戻す処理は禁止。
constexpr std::uint32_t DataPreferredVa(std::uint32_t legacyVa) noexcept
{
    switch (legacyVa) {
    case 0x54eee8: return 0x5b391c; // current mode
    case 0x55d1d0: return 0x5ca9b4; // pending mode
    case 0x55d1d4: return 0x5c4418; // world timer
    case 0x55d203: return 0x5c441f; // pause
    case 0x55d20b: return 0x5ca79a; // intro
    case 0x562a3c: return 0x5c9c58; // round timer (隣接順序が逆転)
    case 0x562a40: return 0x5c9c54; // elapsed round timer
    case 0x5550e0: return 0x5bc324; // round count
    case 0x559548: return 0x5c07a0; // P1 game point
    case 0x55954c: return 0x5c07a4; // P2 game point
    case 0x559550: return 0x5c07a8; // P1 wins
    case 0x559580: return 0x5c07d8; // P2 wins
    case 0x562a64: return 0x5c9c7c; // training pause
    case 0x564b30: return 0x5cb660; // versus pause
    case 0x767440: return 0x7cdab4; // menu allocation/deallocation counter
    case 0x74d598: return 0x7b40a4; // intermediate state
    case 0x74d99c: return 0x7b450c; // skippable state
    case 0x74d8ec: return 0x7b4424; // P1 selection mode
    case 0x74d910: return 0x7b4448; // P2 selection mode
    case 0x74d7f8: return 0x7b4324; // training dummy
    case 0x563778: return 0x5ca9bc; // RNG value
    case 0x56377c: return 0x5ca9c0; // RNG call count
    case 0x564068: return 0x5cb2b8; // RNG index (1..55)
    case 0x564070: return 0x5cb2c0; // RNG 55 DWORD state
    case 0x76e6ac: return 0x5ca9b8; // raw-input pointer slot (値は再配置済み)
    case 0x76e6b0: return 0x7d4df4; // native provider pointer slot
    case 0x555130: return 0x5bc370; // P1 enabled / player root
    case 0x555140: return 0x5bc380; // P1 sequence
    case 0x5551ec: return 0x5bc42c; // P1 health
    case 0x555238: return 0x5bc478; // P1 X
    case 0x55523c: return 0x5bc47c; // P1 Y
    case 0x5552a7: return 0x5bc4e7; // P1 no-input flag
    case 0x5552a8: return 0x5bc4e8; // P1 puppet state
    case 0x555c3c: return 0x5bce7c; // P2 sequence (+0xafc)
    case 0x555ce8: return 0x5bcf28; // P2 health (+0xafc)
    case 0x555d34: return 0x5bcf74; // P2 X (+0xafc)
    case 0x555d38: return 0x5bcf78; // P2 Y (+0xafc)
    case 0x555da3: return 0x5bcfe3; // P2 no-input flag (+0xafc)
    case 0x555da4: return 0x5bcfe4; // P2 puppet state (+0xafc)
    case 0x55689f: return 0x5bdadf; // P3 no-input flag (+2*0xafc)
    case 0x55739b: return 0x5be5db; // P4 no-input flag (+3*0xafc)
    case 0x67bd78: return 0x684cb4; // hit sparks (配列末尾へ移動)
    case 0x5595b8: return 0x5c079c; // attack display
    case 0x5585f8: return 0x5bf83c; // input display
    case 0x564b14: return 0x5cb63c; // camera X (8B SSE copyの下位DWORD)
    case 0x564b18: return 0x5cb640; // camera Y (同上の上位DWORD)
    case 0x76e008: return 0x7d47b0; // 1500 sound flags
    case 0x76c6f8: return 0x7d2d90; // sound-object pointer array
    case 0x74d8f8: return 0x7b4430; // independent selection
    case 0x74d8fc: return 0x7b4434; // independent selection
    case 0x74d900: return 0x7b4438; // independent selection
    case 0x74d904: return 0x7b443c; // independent selection
    case 0x74d91c: return 0x7b4454; // independent selection
    case 0x74d920: return 0x7b4458; // independent selection
    case 0x74d924: return 0x7b445c; // independent selection
    case 0x74d928: return 0x7b4460; // independent selection
    case 0x74fd98: return 0x7b6228; // independent selection
    case 0x74d808: return 0x7b433c; // independent selection
    // vB 1.3追加分。各命令の用途と幅をSteamで再照合。
    case 0x55d1cc: return 0x5c4414; // 47ABAD: simulation frame
    case 0x55df00: return 0x5c5130; // 47ABD4: freeze active
    case 0x558600: return 0x5bf850; // 4CB493: intro fade
    case 0x74605c: return 0x7acb64; // 4CB8CB: intro transition counter
    case 0x74d9b8: return 0x7b4528; // 4CB4DF: P2 transition flag
    case 0x563948: return 0x5cab98; // 4B6320: script RNG index
    case 0x56394c: return 0x5cab9c;
    case 0x563950: return 0x5caba0; // 4B6341: script RNG 55 words
    case 0x54cfc8: return 0x5b4de8; // 4B2F0D/4FBDB2: start display complete
    case 0x54cfcc: return 0x5b4de4;
    case 0x74d83c: return 0x7b4374;
    case 0x74d868: return 0x7b43a0;
    case 0x562a74: return 0x5ca794;
    case 0x77bf2c: return 0x7e9b94;
    default: break;
    }
    // 既にbyte単位で照合した連続保存断片の内部だけを解決する。
    // プレイヤー(+0xAFC)・aux(+0x20C)等の同一構造内offsetを含む。
    const auto find = [legacyVa](const auto &table) constexpr -> std::uint32_t {
        for (const auto &f : table)
            if (legacyVa >= f.legacyVa && legacyVa - f.legacyVa < f.size)
                return 0x400000u + f.steamRva + (legacyVa - f.legacyVa);
        return 0;
    };
    using namespace cccaster::sync;
    if (auto va = find(SteamSnapshotFragments)) return va;
    if (auto va = find(SteamRoundCallAdditionalFragments)) return va;
    if (auto va = find(SteamDisplayFragments)) return va;
    return find(SteamMiscFragments);
}

} // namespace mbaa::steam
