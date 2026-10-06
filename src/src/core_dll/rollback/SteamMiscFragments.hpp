#pragma once
#include "SteamSnapshotLayout.hpp"

namespace cccaster::sync {
// 旧保存表の離散領域を命令の用途から個別照合。値はSteamのRVA。
// 根拠: docs/design/rollback_03_misc.md。実機の保存/復元検証は別途必要。
inline constexpr SteamSnapshotFragment SteamMiscFragments[] = {
    // vB 1.5.1: 拡大補間、透過演出、暗転停止開始。Steam命令ごとに照合。
    {3076, 0x564AFC, 0x1CB624, 4, 0xA144C},
    {3077, 0x564B04, 0x1CB630, 8, 0xB5AE7},
    {3078, 0x564B20, 0x1CB64C, 4, 0xA145B},
    {3079, 0x558604, 0x1BF84C, 4, 0xB5F8F},
    {3080, 0x562A50, 0x1C9C68, 4, 0x108691},
    {3081, 0x5595BC, 0x1C0808, 4, 0x7AB68},
    // vB 1.5追加: ネイティブ更新番号と未処理のSFX要求も再計算開始点へ戻す。
    {3074, 0x55D1CC, 0x1C4414, 4, 0x7ABAD},
    {3075, 0x76E008, 0x3D47B0, 1500, 0x121956},
    {8,    0x559547, 0x1C441C, 1, 0xCDDE9},
    {26,   0x56414C, 0x1CA9C4, 4, 0x7D073},
    {27,   0x5641A4, 0x1CB40C, 4, 0x7C235},
    {28,   0x564200, 0x1CB468, 4, 0x7C38C},
    {3039, 0x74D9D0, 0x3B4358, 4, 0xE8524},
    {3057, 0x76E6FC, 0x1CB650, 4, 0xA1668},
    {3059, 0x7717D8, 0x3D8C74, 4, 0x7A7AE},
    // CRT CPU機能選択値。旧boolに対しSteamはISA段階値。
    // 値を旧版/他プロセスから移植しない。同一プロセス内でだけ保存/復元する。
    {3060, 0x7B1D2C, 0x1BA028, 4, 0x159A10},
    {3064, 0x55DEC2, 0x1C5106, 1, 0xCDE1B},
    {3064, 0x55DEC3, 0x1C9C96, 1, 0xD0289},
};
} // namespace cccaster::sync
