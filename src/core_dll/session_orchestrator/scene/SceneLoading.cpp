// ============================================================================
// SceneLoading — ロード画面
//
// 【責務】
//   CentralBuffer から自入力・相手入力を読取り、ゲームメモリに書込む。
//   ディレイ計算は通信スレッド(SyncCoordinator)がバッファ書込み時に処理済み。
//   DLL 側は一切ディレイ計算を行わない。
//
// 【入力ソース】
//   CentralBuffer.GetReadPos() → GetSlot(readPos) のみ。
//   readPos = writeHead - delay - maxRollback
//   非ロールバック区間のため confirmed=true のスロットのみ消費。
//   未確定なら入力クリアして待つ。
// ============================================================================

#include "core_dll/session_orchestrator/scene/SceneLoading.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/session_orchestrator/session/GameControl.hpp"
#include "core_dll/pure_sync_engine/CentralBuffer.hpp"

namespace cccaster::domain::scene {

using GC = cccaster::domain::session::GameControl;
using cccaster::domain::session::DebugLog;
using cccaster::domain::session::SceneRunner;

void SceneLoading::Reset() {
    // 状態なし — CentralBuffer が全管理
}

// ReadAndSend — Phase A: 入力送信
// 入力送信は通信スレッド(SyncCoordinator)が担当するため、DLL 側は何もしない。
void SceneLoading::ReadAndSend(session::SessionContext& ctx,
                                const SceneRunner::SendFunc& send) {
    (void)ctx;
    (void)send;
}

// ProcessFrame — Phase B: CentralBuffer からの入力読取り + ゲームメモリ書込み
void SceneLoading::ProcessFrame(session::SessionContext& ctx) {
    auto& buf = cccaster::core::sync::CentralBuffer::GetInstance();

    uint32_t readPos = buf.GetReadPos();

    // readPos が有効範囲外（初期状態で writeHead < delay+maxRollback）
    if (readPos == 0) {
        GC::ClearInput();
        return;
    }

    const auto& slot = buf.GetSlot(readPos);

    // 非ロールバック区間: confirmed のみ消費。未確定なら待つ。
    if (!slot.confirmed) {
        GC::ClearInput();
        return;
    }

    // P1/P2 振り分け + ゲームメモリ書込み
    uint32_t p1, p2;
    if (ctx.isHost) {
        p1 = slot.localInput;
        p2 = slot.remoteInput;
    } else {
        p1 = slot.remoteInput;
        p2 = slot.localInput;
    }
    GC::WriteInput(p1, p2);
}

} // namespace cccaster::domain::scene
