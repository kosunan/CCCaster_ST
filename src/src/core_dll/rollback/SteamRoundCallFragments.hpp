#pragma once
#include "SteamSnapshotLayout.hpp"

namespace cccaster::sync {
// node 3040〜3047 の不足分だけ。既存 SteamSnapshotFragments と重複しない。
// 静的照合根拠: docs/design/rollback_01_roundcall.md。
// この配列単独では完全な保存表にも実機検証済みの表にもならない。
inline constexpr SteamSnapshotFragment SteamRoundCallAdditionalFragments[] = {
    {3040, 0x74E4E4, 0x3B4ABC, 4, 0xFF3E0},
    {3040, 0x74E4E8, 0x3B4AB8, 4, 0xFF3F4},
    {3041, 0x74E5B0, 0x3B4BE0, 4, 0xFEC44},
    {3043, 0x74E778, 0x3B4BB4, 4, 0xFD288},
    {3044, 0x74E78C, 0x3B4BCC, 4, 0xFEA4D},
    {3044, 0x74E790, 0x3B4BC8, 4, 0xFE9D1},
    {3044, 0x74E794, 0x3B4BD4, 4, 0xFEC7C},
    {3045, 0x74E79C, 0x3B4BDC, 4, 0xFEA3D},
    {3045, 0x74E7A0, 0x3B4BD8, 4, 0xFEACC},
    {3046, 0x74E7B4, 0x3B4DBC, 4, 0xFEB9B},
    {3046, 0x74E7BC, 0x3B4DC8, 4, 0xFEBBF},
    {3047, 0x74E7C8, 0x3B4DD0, 4, 0xFEC4F},
    {3047, 0x74E7D0, 0x3B4DD8, 4, 0xFE99B},
    {3047, 0x74E7D4, 0x3B4DD4, 4, 0xFEB2C},
};
} // namespace cccaster::sync
