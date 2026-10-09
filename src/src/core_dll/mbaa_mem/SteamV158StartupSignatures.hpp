#pragma once
#include "SteamCodeSignature.hpp"
// Steam 2017-01-05。呼出規約/データ構造を静的照合し、HIGHLOW再配置を正規化。
namespace cccaster::game_memory::steam_v158_startup {
inline constexpr uint16_t CpuInitRelocations[]{0x5,0xf,0x1e,0x2f,0x38,0x41,0x47,0x51};
inline constexpr steam_code::Signature CpuInit{0x483fc0,0x5d,0x5309eaa9f37e4cc5ull,CpuInitRelocations};
inline constexpr uint16_t CpuEntryRelocations[]{0x3,0x10,0x1f};
inline constexpr steam_code::Signature CpuEntry{0x4853a4,0x29,0x46d1faa3e939c9d2ull,CpuEntryRelocations};
inline constexpr steam_code::Signature BootDispatch{0x48b43a,0xa,0xa0bccfc46a991ef1ull,{}};
inline constexpr uint16_t LogoInitRelocations[]{0x3,0x9,0x12,0x17,0x1d,0x2c,0x3c,0x4a,0x50,0x5e,0x67,0x6c,0x7a};
inline constexpr steam_code::Signature LogoInit{0x4865e0,0xa0,0x7be67ae1c9646f2full,LogoInitRelocations};
inline constexpr uint16_t SettingsDialogTailRelocations[]{0x1,0xd,0x14};
inline constexpr steam_code::Signature SettingsDialogTail{0x4f30b9,0x1c,0x21adf256d65b6146ull,SettingsDialogTailRelocations};
inline constexpr uint16_t SoundInitRelocations[]{0xa,0x1a,0x43,0x54,0x61,0x70,0x7b,0xa6,0xab,0xb5,0xc8};
inline constexpr steam_code::Signature SoundInit{0x521110,0xf9,0x7996a07f435963c2ull,SoundInitRelocations};
inline constexpr uint16_t SoundLoadRelocations[]{0x9,0x1b,0x3d,0x63,0x72,0xb2,0xfc,0x11f,0x12a};
inline constexpr steam_code::Signature SoundLoad{0x520f90,0x180,0xa3aaea93bac38f46ull,SoundLoadRelocations};
inline constexpr uint16_t SoundNameRelocations[]{0x11,0x25,0x52};
inline constexpr steam_code::Signature SoundName{0x520bf0,0x71,0xe7692b9431d0135cull,SoundNameRelocations};
inline constexpr steam_code::Signature SoundVolume{0x466d20,0x45,0xdcedcba8456969e9ull,{}};
inline constexpr uint16_t SoundStartupCallRelocations[]{0xd,0x1e,0x24};
inline constexpr steam_code::Signature SoundStartupCall{0x474ec0,0x41,0x070de9b356ebc25cull,SoundStartupCallRelocations};
inline constexpr uint16_t NativeInputInitRelocations[]{0xf,0x14,0x21,0x27,0x2e};
inline constexpr steam_code::Signature NativeInputInit{0x4efed0,0x6f,0x1b407d6b3bfe815bull,NativeInputInitRelocations};
inline constexpr steam_code::Signature NativeInputMap{0x4efe80,0x4f,0xc3606f3e5c137fb1ull,{}};
inline constexpr uint16_t NativeInputEnumRelocations[]{0x2,0x1f,0x26,0x2d,0x32};
inline constexpr steam_code::Signature NativeInputEnum{0x467820,0x45,0x3c3beaa0f3a26564ull,NativeInputEnumRelocations};
inline constexpr uint16_t NativeInputPollPadsRelocations[]{0xb,0x17,0x85,0x9b,0xa6};
inline constexpr steam_code::Signature NativeInputPollPads{0x4683e0,0xda,0xc234bdf5045df208ull,NativeInputPollPadsRelocations};
inline constexpr uint16_t NativeInputPollKeysRelocations[]{0xa,0x16,0x27,0x36,0x71,0x82,0xa8,0xbd};
inline constexpr steam_code::Signature NativeInputPollKeys{0x4684c0,0xcc,0xc0b9d69541d01409ull,NativeInputPollKeysRelocations};
inline constexpr uint16_t NativeInputPollMouseRelocations[]{0x7,0x11,0x1b,0x23,0x33,0x3d,0x47,0x51,0xa0,0xa6,0xab,0xb3,0xbb,0xd2,0xe1,0xef,0xf5,0x101};
inline constexpr steam_code::Signature NativeInputPollMouse{0x468590,0x110,0xc5b2c99fc5fe36baull,NativeInputPollMouseRelocations};
inline constexpr uint16_t NativeInputDestroyRelocations[]{0x6,0x13,0x23,0x2d,0x39,0x87,0x8d,0xb9,0xca,0xd3,0xde,0xe4,0xee,0xfe};
inline constexpr steam_code::Signature NativeInputDestroy{0x467710,0x10a,0xd1d09ae8e9ca1ad7ull,NativeInputDestroyRelocations};
inline constexpr uint16_t BossFileRelocations[]{0xa};
inline constexpr steam_code::Signature BossFile{0x519db0,0x24,0x4d89b5a493641dd2ull,BossFileRelocations};
inline constexpr uint16_t BossPreviewLoadRelocations[]{0xa};
inline constexpr steam_code::Signature BossPreviewLoad{0x49f920,0x50,0xf8b4807d596b8f12ull,BossPreviewLoadRelocations};
inline constexpr uint16_t BossPreviewCallerRelocations[]{0x14,0x83,0x8e,0xc4};
inline constexpr steam_code::Signature BossPreviewCaller{0x4e3a89,0xf0,0xc69f19683452b8e2ull,BossPreviewCallerRelocations};
inline constexpr uint16_t BossPaletteRelocations[]{0xa,0x17,0x52,0x5d};
inline constexpr steam_code::Signature BossPalette{0x4dfef0,0xa6,0x2824c58f8f2c8cddull,BossPaletteRelocations};
inline constexpr uint16_t BossScaleRelocations[]{0xa,0x67,0x10a,0x110};
inline constexpr steam_code::Signature BossScale{0x4dffa0,0x114,0x30097db6320c6459ull,BossScaleRelocations};
inline constexpr uint16_t BossGridRelocations[]{0x1a,0x8b,0x93,0x9a,0xa1,0xb3};
inline constexpr steam_code::Signature BossGrid{0x4e5590,0xc2,0x50ab88014c075a00ull,BossGridRelocations};
inline constexpr steam_code::Signature BossIconTable{0x5897e8,0x194,0xc7eeae536752fcc5ull,{}};
inline constexpr steam_code::Signature BossNameTable{0x589980,0x194,0xa130278cfaacfea0ull,{}};
inline constexpr steam_code::Signature Sounds[]{SoundInit,SoundLoad,SoundName,SoundVolume,SoundStartupCall};
inline constexpr steam_code::Signature NativeInput[]{NativeInputInit,NativeInputMap,NativeInputEnum,NativeInputPollPads,NativeInputPollKeys,NativeInputPollMouse,NativeInputDestroy};
inline constexpr steam_code::Signature BossSelection[]{BossFile,BossPreviewLoad,BossPreviewCaller,BossPalette,BossScale,BossGrid,BossIconTable,BossNameTable};
inline constexpr steam_code::Signature All[]{CpuInit,CpuEntry,BootDispatch,LogoInit,SettingsDialogTail,SoundInit,SoundLoad,SoundName,SoundVolume,SoundStartupCall,NativeInputInit,NativeInputMap,NativeInputEnum,NativeInputPollPads,NativeInputPollKeys,NativeInputPollMouse,NativeInputDestroy,BossFile,BossPreviewLoad,BossPreviewCaller,BossPalette,BossScale,BossGrid,BossIconTable,BossNameTable};
}
