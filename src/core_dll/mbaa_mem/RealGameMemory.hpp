#pragma once
/**
 * @file RealGameMemory.hpp
 * @brief MBAA プロセスの実メモリに読み書きする IGameMemory 実装
 *
 * DLL 側でのみ使う。DLL 初期化時に InstallRealGameMemory() を1回呼ぶ。
 */

#include "core_dll/mbaa_mem/IGameMemory.hpp"

namespace cccaster::game_interface {

class RealGameMemory final : public IGameMemory {
public:
    bool     IsAvailable()      const override;
    uint32_t GameMode()         const override;
    uint8_t  IntroState()       const override;
    uint32_t WorldTimer()       const override;
    uint32_t RealTimer()        const override;
    uint32_t MenuStateCounter() const override;
    void     WriteInput(GameInput p1, GameInput p2) override;
};

/// 実メモリ実装を設置する。DLL 初期化の最初期に1回だけ呼ぶ。
void InstallRealGameMemory();

} // namespace cccaster::game_interface
