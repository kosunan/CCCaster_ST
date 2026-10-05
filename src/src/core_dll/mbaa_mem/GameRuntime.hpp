#pragma once
#include "shared_contracts/GameBuild.hpp"
#include "shared_contracts/GameImageAddress.hpp"
#include "SteamAddressMap.hpp"

namespace cccaster::game_memory {
// 版照合後、フックと実メモリ実装を設置する前に一度だけ確定する。
// 起動中や対戦中に版・基準位置を切り替えない。
class GameRuntime {
public:
    static bool Initialize(game_build::Edition edition, game_build::LoadedImage image) {
        if (edition_ != game_build::Edition::Unknown || !image.base ||
            edition != game_build::Edition::Steam20170105 || image.size != 0xf3e000) return false;
        image_ = image;
        edition_ = edition;
        return true;
    }
    static game_build::Edition Edition() { return edition_; }
    static game_build::LoadedImage Image() { return image_; }
    static bool IsSteam() { return edition_ == game_build::Edition::Steam20170105; }
    static uintptr_t Preferred(uint32_t va, uint32_t bytes = 1) {
        return va >= 0x400000 ? image_.Resolve(va - 0x400000, bytes) : 0;
    }
    // Steam設定は静的配列ではなくnative config pointer経由。
    // 49F87B..49F89C: Versus damage +0xC / wins +0x1C / timer +0x10。
    static uintptr_t Config(unsigned offset) {
        const auto slot = image_.Resolve(0x1bb170, 4);
        if (!slot || offset >= 0x1ec) return 0;
        const auto base = *reinterpret_cast<const uint32_t *>(slot);
        return base ? uintptr_t(base) + offset : 0;
    }
    static uintptr_t Address(uint32_t legacyVa, uint32_t bytes = 1) {
        if (edition_ == game_build::Edition::Unknown) return 0;
        return Preferred(::mbaa::steam::DataPreferredVa(legacyVa), bytes);
    }
private:
    inline static game_build::Edition edition_ = game_build::Edition::Unknown;
    inline static game_build::LoadedImage image_{};
};
}
