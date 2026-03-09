#pragma once
// ============================================================================
// SceneRematch — リマッチ画面（レガシー踏襲）
// ============================================================================

#include <cstdint>
#include "core_dll/session_orchestrator/session/SessionContext.hpp"
#include "core_dll/session_orchestrator/session/SceneRunner.hpp"

namespace cccaster::domain::scene {

class SceneRematch {
public:
    /// @brief Phase A: ローカル入力読取 + REMATCH_MENU 送信（SleepFrame 前に呼ぶ）
    static void ReadAndSend(session::SessionContext& ctx,
                            const session::SceneRunner::SendFunc& send);

    /// @brief Phase B: 受信処理 + ゲームロジック更新（SleepFrame 後に呼ぶ）
    static void ProcessFrame(session::SessionContext& ctx);

    static void Reset();

    // リモートからのメニュー選択受信API（PacketRouter から呼ばれる）
    // スレッドセーフ（atomic 書き込み）
    static void SetRemoteRetryMenuIndex(int8_t menuIndex);
};

} // namespace
