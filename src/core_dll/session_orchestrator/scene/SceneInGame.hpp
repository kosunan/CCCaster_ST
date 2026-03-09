#pragma once
// ============================================================================
// SceneInGame — 対戦画面（最複雑）
// ロールバックエンジン初期化+始動、禁止区間ガード、ラウンド同期
// ============================================================================

#include <cstdint>
#include "core_dll/session_orchestrator/session/SessionContext.hpp"
#include "core_dll/session_orchestrator/session/SceneRunner.hpp"    // SendFunc 型

namespace cccaster::sync { class RollbackEngine; }

namespace cccaster::domain::scene {

class SceneInGame {
public:
    /// @brief Phase A: ローカル入力読取 + GAME_INPUT 送信（SleepFrame 前に呼ぶ）
    static void ReadAndSend(session::SessionContext& ctx,
                            cccaster::sync::RollbackEngine& re,
                            const session::SceneRunner::SendFunc& send);

    /// @brief Phase B: 受信処理 + ゲームロジック更新（SleepFrame 後に呼ぶ）
    static void ProcessFrame(session::SessionContext& ctx,
                             cccaster::sync::RollbackEngine& re);

    static void Reset();   // 画面遷移時のリセット
};

} // namespace cccaster::domain::scene

