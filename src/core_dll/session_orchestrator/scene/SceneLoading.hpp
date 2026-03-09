#pragma once
// ============================================================================
// SceneLoading — ロード画面
// SyncCoordinator Resetのみ（OnPhaseChangedで実施済み）
// ============================================================================

#include "core_dll/session_orchestrator/session/SessionContext.hpp"
#include "core_dll/session_orchestrator/session/SceneRunner.hpp"
#include <cstdint>

namespace cccaster::domain::scene {

class SceneLoading {
public:
    /// @brief Phase A: ローカル入力読取 + LOADING_INPUT 送信（SleepFrame 前に呼ぶ）
    static void ReadAndSend(session::SessionContext& ctx,
                            const session::SceneRunner::SendFunc& send);

    /// @brief Phase B: 受信処理 + ゲームロジック更新（SleepFrame 後に呼ぶ）
    static void ProcessFrame(session::SessionContext& ctx);

    static void Reset();   // 画面遷移時のリセット

    /// @brief PacketRouter から呼ばれるリモート入力受信 API
    /// スレッドセーフ（atomic 書き込み）
    static void SetRemoteLoadingInput(uint16_t input);
};

} // namespace
