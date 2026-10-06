#pragma once
#include "SteamCodeSignature.hpp"
// Steam 2017-01-05 (SHA-256 11270cf2...), PE HIGHLOWを正規化した命令署名。
namespace cccaster::game_memory::steam_v15 {
inline constexpr uint16_t DirectEntry0Relocations[]{0x2,0x8,0x1c,0x2c,0x55,0x68,0x78,0x7d,0x88,0x9c,0xa2,0xac,0xc0,0xca,0xe6,0xf1,0xf9,0xff,0x111,0x11f,0x131};
inline constexpr uint16_t DirectEntry1Relocations[]{0x2,0xc};
inline constexpr uint16_t DirectEntry2Relocations[]{0x2};
inline constexpr steam_code::Signature DirectEntry[]{
    {0x485580,0x145,0x494805ddc4fa0408ull,DirectEntry0Relocations},
    {0x485791,0x14,0x448399020095e281ull,DirectEntry1Relocations},
    {0x4856f3,0xf,0x29b293012298c450ull,DirectEntry2Relocations}
};
inline constexpr uint16_t FileRead1Relocations[]{0x2,0xc};
inline constexpr uint16_t FileRead2Relocations[]{0x2d};
inline constexpr uint16_t FileRead3Relocations[]{0x74,0x8d,0xa9,0xc5,0xeb,0xf8};
inline constexpr steam_code::Signature FileRead[]{
    {0x46b3c0,0x115,0xc4a0cca1dec763d4ull,{}},
    {0x46b4db,0x1f,0xb560cda45334274dull,FileRead1Relocations},
    {0x46b500,0x40,0x0c6aebd2dcb12d49ull,FileRead2Relocations},
    {0x46b770,0x12a,0xd73d9474f199b886ull,FileRead3Relocations}
};
inline constexpr uint16_t TrainingMenu0Relocations[]{0x6,0x18};
inline constexpr uint16_t TrainingMenu1Relocations[]{0x6,0x16};
inline constexpr uint16_t TrainingMenu2Relocations[]{0x6,0x16};
inline constexpr uint16_t TrainingMenu5Relocations[]{0x3};
inline constexpr uint16_t TrainingMenu6Relocations[]{0xa};
inline constexpr uint16_t TrainingMenu8Relocations[]{0xa};
inline constexpr uint16_t TrainingMenu9Relocations[]{0x6,0x15};
inline constexpr steam_code::Signature TrainingMenu[]{
    {0x4d6320,0x20,0xe5f8824f2b508c41ull,TrainingMenu0Relocations},
    {0x47a370,0x20,0xfd89e5dea6bc8462ull,TrainingMenu1Relocations},
    {0x435100,0x20,0x6fabc3a32a0028d1ull,TrainingMenu2Relocations},
    {0x41f960,0x20,0x993cbd964574e88full,{}},
    {0x4a0150,0x20,0x072237fd0446710dull,{}},
    {0x472ff0,0x2e,0x2315d92d8251dbf0ull,TrainingMenu5Relocations},
    {0x49f9e0,0x20,0x3b351c74f2843577ull,TrainingMenu6Relocations},
    {0x46b2f0,0x20,0xac4d968faa93462aull,{}},
    {0x519db0,0x20,0x18d2689fdae9bbf6ull,TrainingMenu8Relocations},
    {0x51fbd0,0x20,0xe17e20d3dd46f5eeull,TrainingMenu9Relocations}
};
inline constexpr uint16_t TrainingInitRelocations[]{0x5,0xf,0x25,0x2e,0x37,0x3d,0x47,0x51};
inline constexpr steam_code::Signature TrainingInit{0x483ed0,0x5d,0x374e48c3032c2f20ull,TrainingInitRelocations};
inline constexpr uint16_t VersusInitRelocations[]{0x5,0xf,0x1e,0x2f,0x38,0x41,0x47,0x51};
inline constexpr steam_code::Signature VersusInit{0x483f60,0x5d,0xab8988f2cbdba1f5ull,VersusInitRelocations};
inline constexpr steam_code::Signature SweepReset{0x4b6700,0x20,0xf191980d308a2677ull,{}};
inline constexpr steam_code::Signature SweepApply{0x4b6890,0x20,0x7f1e2e53b96094f5ull,{}};
}
