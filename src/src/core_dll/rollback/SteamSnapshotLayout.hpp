#pragma once
#include <cstddef>
#include <cstdint>

namespace cccaster::sync {
// Steam 2017-01-05 版の主保存断片。追加3表とSteamSnapshotBuilderで組み立てる。
// docs/design/rollback_01_roundcall.md、02_display、03_miscを参照。
// source は RVA。ゲームが格納したポインターにベース差分を加算しない。
inline constexpr bool SteamSnapshotLayoutComplete = true;
inline constexpr std::size_t SteamSnapshotLegacyRootCount = 61;
inline constexpr std::size_t SteamSnapshotAdditionalRootCount = 6;
struct SteamSnapshotFragment {
    std::uint32_t legacyNode;
    std::uint32_t legacyVa;
    std::uint32_t steamRva;
    std::uint32_t size;
    std::uint32_t evidenceInstructionRva;
};
inline constexpr SteamSnapshotFragment SteamSnapshotFragments[] = {
    {0, 0x54EB70, 0x1B3B94, 4, 0xA154D},
    {0, 0x54EB74, 0x1B3A28, 4, 0xA1543},
    {0, 0x54EB78, 0x1B3B90, 4, 0xA1557},
    {1, 0x555124, 0x1BC35C, 8, 0xA151C},
    {2, 0x555130, 0x1BC370, 11248, 0x72F2B},
    {3, 0x557D2B, 0x1BB7F3, 1, 0xA1572},
    {4, 0x557DAC, 0x1BEFFC, 2, 0xA179C},
    {5, 0x557DB0, 0x1BEFF4, 4, 0xA1C42},
    {5, 0x557DB4, 0x1BEFF8, 4, 0xA1C62},
    {5, 0x557DB8, 0x1BF000, 1048, 0xBD5D8},
    {6, 0x5585E8, 0x1BF830, 12, 0xA1536},
    {7, 0x558608, 0x1BF860, 3900, 0x73790},
    {8, 0x559546, 0x1BEF6B, 1, 0xA1765},
    {8, 0x559548, 0x1C07A0, 12, 0x7DD01},
    {9, 0x559580, 0x1C07D8, 4, 0x7DCF1},
    {10, 0x5595B4, 0x1BF858, 4, 0x7372C},
    {11, 0x55D1D4, 0x1C4418, 4, 0x8A78C},
    {12, 0x55D204, 0x1C4448, 4, 0xA19F6},
    {12, 0x55D208, 0x1C444C, 2, 0xCEE10},
    {13, 0x55D20B, 0x1CA79A, 1, 0xCB6F7},
    {14, 0x55DEC4, 0x1C9C84, 12, 0xA150F},
    {15, 0x55DEDC, 0x1C5114, 12, 0xA1CD4},
    {15, 0x55DEE8, 0x1C5120, 4, 0xA1A2D},
    {16, 0x562A3C, 0x1C9C58, 4, 0x7B7EC},
    {16, 0x562A40, 0x1C9C54, 4, 0x7374F},
    {17, 0x562A48, 0x1C9C60, 4, 0x7373B},
    {18, 0x562A6C, 0x1C9C94, 2, 0x7ACD6},
    {19, 0x562A6F, 0x1C513F, 1, 0x7377B},
    {20, 0x56357C, 0x1CA7A4, 4, 0xA1A24},
    {20, 0x563580, 0x1CA7B0, 96, 0xCEDD2},
    {21, 0x5635F4, 0x1CA824, 96, 0xCEDF5},
    {22, 0x563750, 0x1CA7A0, 4, 0xA1C39},
    {23, 0x563778, 0x1CA9BC, 8, 0x799C8},
    {24, 0x563864, 0x1CAAB4, 4, 0xD019C},
    {25, 0x564068, 0x1CB2B8, 4, 0x79990},
    {26, 0x564070, 0x1CB2C0, 220, 0x799B3},
    {29, 0x564AF8, 0x1CB620, 4, 0xB5ADF},
    {30, 0x564B00, 0x1CB62C, 2, 0x73770},
    {31, 0x564B0C, 0x1CB638, 4, 0xA1502},
    {31, 0x564B10, 0x1CB648, 2, 0x7376A},
    {32, 0x564B14, 0x1CB63C, 12, 0xA1529},
    {33, 0x564B24, 0x1CB654, 4, 0xA1A6F},
    {34, 0x61E170, 0x284CE0, 384000, 0x7DCA2},
    {35, 0x67BD78, 0x284CB4, 4, 0xADBBD},
    {36, 0x67BDE8, 0x2E28F0, 828000, 0x7DC91},
    {3037, 0x74D598, 0x3B40A4, 4, 0xCB674},
    {3038, 0x74D99C, 0x3B450C, 4, 0x74D00},
    // RoundCall: リソース文字列と描画/更新条件を照合。旧隣接順は非互換。
    {3042, 0x74E768, 0x3B4BA8, 4, 0xFD157},
    {3043, 0x74E770, 0x3B4BB0, 4, 0xFEC84},
    {3043, 0x74E774, 0x3B4BAC, 4, 0xFE903},
    {3043, 0x74E77C, 0x3B4BBC, 4, 0xFE8D3},
    {3043, 0x74E780, 0x3B4BB8, 4, 0xFD22A},
    {3045, 0x74E7A4, 0x3B4DAC, 4, 0xFD189},
    {3046, 0x74E7AC, 0x3B4DB4, 4, 0xFD175},
    {3046, 0x74E7B0, 0x3B4DB0, 4, 0xFEC99},
    {3046, 0x74E7B8, 0x3B4DB8, 4, 0xFD12F},
    {3047, 0x74E7CC, 0x3B4DCC, 4, 0xFD193},
    {3048, 0x74E7DC, 0x3B4DE4, 4, 0xFC962},
    {3049, 0x74E7E4, 0x3B4DEC, 16, 0xFC82F},
    {3057, 0x76E6F4, 0x1CB628, 4, 0xA1561},
    {3057, 0x76E6F8, 0x1CB61C, 4, 0xA1566},
    {3061, 0x557D2A, 0x1BB7F2, 1, 0x7ACB1},
    {3062, 0x563574, 0x1CA798, 2, 0x7AC98},
    {3063, 0x562A70, 0x1C9C90, 4, 0x7ACF3},
    {3064, 0x55DEC0, 0x1C5104, 2, 0x7AD02},
    {3065, 0x55DEF0, 0x1C5124, 4, 0xCB68F},
    {3066, 0x55DF24, 0x1C9C50, 4, 0xCB687},
};
// vBのイントロRBで追加保存するSteam固有の状態（2026-09-15命令照合）。
inline constexpr SteamSnapshotFragment SteamIntroFragments[] = {
    {3067, 0x74605C, 0x3ACB64, 4, 0xCB8CB},
    {3068, 0x74D9B8, 0x3B4528, 4, 0xCB4DF},
    {3069, 0x558600, 0x1BF850, 4, 0xCB493},
    {3070, 0x563948, 0x1CAB98, 228, 0xB6320},
    {3071, 0x54CFC8, 0x1B4DE8, 4, 0xB2F0D},
    {3071, 0x54CFCC, 0x1B4DE4, 4, 0xB2F01},
    // 479BB0(ECX=1)/4FC1F6: 演出乱数の55語と予約語。indexだけの復元では
    // まばたき間隔(7B4E60)が再計算で変化する。indexは主表node24で保存済み。
    {3072, 0x563868, 0x1CAAB8, 224, 0xD01C2},
    // RoundCallの番号/種類。4FD114の初期化分岐が再計算でも同じ値を読む。
    {3073, 0x54CFE4, 0x1B4DD4, 4, 0xFD114},
};
// 0x4AB730 の空きスロット走査と 0x47DC91 の memset で照合。
inline constexpr std::uint32_t SteamSnapshotObjectPoolRva = 0x2E28F0;
inline constexpr std::uint32_t SteamSnapshotObjectStride = 0x33C;
inline constexpr std::uint32_t SteamSnapshotObjectCount = 1000;
inline constexpr std::uint32_t SteamSnapshotObjectPoolBytes = 0xCA260;
static_assert(SteamSnapshotObjectStride * SteamSnapshotObjectCount == SteamSnapshotObjectPoolBytes);
// 第1段は object + 4 を this とする 0x4A9AE8 の +0x31C、続く +0x38。
// 子ノードはSteamSnapshotPointersの資源世代監視と組み合わせて生成する。
inline constexpr std::uint32_t SteamSnapshotObjectPointerOffset = 0x320;
inline constexpr std::uint32_t SteamSnapshotFirstPointeeOffset = 0x38;
} // namespace cccaster::sync
