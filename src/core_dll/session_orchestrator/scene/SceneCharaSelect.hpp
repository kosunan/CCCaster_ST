#pragma once
// ============================================================================
// SceneCharaSelect — キャラクターセレクト画面
// 同期方式: 確定応答方式（LockStep Confirm）
// ============================================================================

#include "core_dll/session_orchestrator/session/SessionContext.hpp"
#include "core_dll/session_orchestrator/session/SceneRunner.hpp"
#include <cstdint>

namespace cccaster::domain::scene {

class SceneCharaSelect {
public:
    /// @brief Phase A: ローカル入力読取 + パケット送信（SleepFrame 前に呼ぶ）
    static void ReadAndSend(session::SessionContext& ctx,
                            const session::SceneRunner::SendFunc& send);

    /// @brief Phase B: 受信処理 + ゲームロジック更新（SleepFrame 後に呼ぶ）
    static void ProcessFrame(session::SessionContext& ctx);

    static void Reset();   // 画面遷移時のリセット
};

} // namespace cccaster::domain::scene
