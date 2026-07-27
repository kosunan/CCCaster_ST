#pragma once
/**
 * @file IGameMemory.hpp
 * @brief ゲームメモリへの読み書き口
 *
 * 【なぜ挟むか】
 *   同期ロジックが `*CC_XXX_ADDR` を直接触っている限り、MBAA を起動しないと
 *   一行も検証できない。フェーズ遷移・IntroBarrier・Rematch の状態機械は
 *   すべてこの読み書きの上に乗っているため、ここを差し替え可能にすると
 *   ゲーム無しで回せるようになる。
 *
 * 【実装は2つだけ】
 *   RealGameMemory (core_dll)  — 実アドレスへの読み書き
 *   FakeGameMemory (src/tests) — 値を明示指定し、書き込まれた入力を記録する
 *
 * 【呼び出しコスト】
 *   仮想呼び出し1回あたり約1-2ns。毎フレーム10回でも 1F(16.7ms) の 0.0001%。
 *   本プロジェクトが禁じている「無駄なコピー・ロック待機」には当たらない。
 *
 * 【seam に含めないもの】
 *   FastBoot のコード書換 (CC_FORCE_GOTO_ADDR) と SFX 配列クリア、
 *   起動時1回の MbaaPatcher、ステート保存 (DumpEntryList) は対象外。
 *   テストでは FastBoot 自体をスキップする。
 */

#include <cstdint>
#include "core_dll/mbaa_mem/GameInput.hpp"

namespace cccaster::game_interface {

class IGameMemory {
public:
    virtual ~IGameMemory() = default;

    /// ゲームのメモリがまだマップされておらず読めない状態を区別する。
    /// 起動直後やプロセス終了間際に false になりうる。
    virtual bool IsAvailable() const = 0;

    // ── 読み取り ──
    virtual uint32_t GameMode()         const = 0;  ///< CC_GAME_MODE_*
    virtual uint8_t  IntroState()       const = 0;  ///< 2=イントロ演出中 1=pre-game 0=対戦進行中
    virtual uint32_t WorldTimer()       const = 0;  ///< 常時カウントアップ
    virtual uint32_t RealTimer()        const = 0;  ///< ラウンド開始後にカウントアップ
    virtual uint32_t MenuStateCounter() const = 0;  ///< メニュー階層のスタック深度

    // ── 書き込み ──
    virtual void WriteInput(GameInput p1, GameInput p2) = 0;
};

/// プロセス起動時に1回だけ差し込む。nullptr を渡すと未設置状態に戻る。
void InstallGameMemory(IGameMemory* impl);

/// 現在設置されている実装。未設置なら値を返さず書込みも捨てる実装が返る。
IGameMemory& GameMem();

} // namespace cccaster::game_interface
