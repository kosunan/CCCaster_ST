#pragma once
/**
 * @file ISpeedController.hpp
 * @brief ゲーム速度制御の抽象インターフェース
 *
 * 【責務】
 *   ゲームの進行速度や一時停止など、スピードと描画状態を統合管理する。
 */

#include <cstdint>

namespace cccaster::core {

/**
 * @brief ゲーム描画および速度モードを表現する列挙型
 */
enum class SpeedMode {
    HighSpeedSkip_Normal,   // ゲーム起動時、キャラセレ画面の最初50Fで使用 (CC_SKIP=1, Sleep=0ms)
    HighSpeedSkip_Rollup,   // ロールアップ時 (CC_SKIP=N, Sleep=0ms)
    NormalSpeed,            // 通常速度 (CC_SKIP=1, Sleep=16.66ms)
    Pause                   // 一時停止 (CC_SKIP=1, Pause=1, Sleep=16.66ms)
};

/**
 * @brief ゲーム速度制御の抽象インターフェース
 */
class ISpeedController {
public:
    virtual ~ISpeedController() = default;

    /**
     * @brief 速度・描画モードを設定する
     * @param[in] mode 新しいモード
     * @param[in] rollupFrames ロールアップ時のフレーム数（HighSpeedSkip_Rollup時のみ使用）
     */
    virtual void SetMode(SpeedMode mode, uint32_t rollupFrames = 0) = 0;

    /**
     * @brief 現在のモードに応じたメモリアドレスの状態を毎フレーム維持する
     * @details
     *   ゲームエンジンがスキップカウンタ等を消費するため毎フレーム再設定が必要。
     */
    virtual void MaintainState() = 0;

    /**
     * @brief 現在のモードを取得する
     */
    virtual SpeedMode GetMode() const = 0;

    /**
     * @brief 現在描画をスキップすべき高速モード状態かを取得する
     */
    virtual bool IsSkipMode() const = 0;
};

} // namespace cccaster::core
