// ============================================================================
// SceneLoading — ロード画面
// 設計書: docs/design/core_dll/scene_business_logic.md §6
//
// 【3層アーキテクチャにおける位置づけ】
//   Layer 3 (Scene ビジネスロジック)
//   ゲーム制御は GameControl:: ファサードを通じて行う。
//
// 【責務】
//   CentralBuffer から自入力・相手入力を読取り、ゲームメモリに書込む。
//   ディレイ計算は通信スレッド(SyncCoordinator)がバッファ書込み時に処理済み。
//   DLL 側は一切ディレイ計算を行わない。
//
// 【入力ソース】
//   CentralBuffer.GetSlot(playHead) のみ。
//   PacketRouter 経由の SetRemoteLoadingInput は廃止。
// ============================================================================

#include "core_dll/session_orchestrator/scene/SceneLoading.hpp"
#include "core_dll/session_orchestrator/session/DebugLog.hpp"
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
    // 通信スレッドが CentralBuffer に書込み + パケット送信済み
}

// ProcessFrame — Phase B: CentralBuffer からの入力読取り + ゲームメモリ書込み
void SceneLoading::ProcessFrame(session::SessionContext& ctx) {
    auto& buf = cccaster::core::sync::CentralBuffer::GetInstance();

    uint32_t playHead = buf.GetPlayHead();
    uint32_t writeHead = buf.GetWriteHead();

    // バッファに未処理データがなければスキップ
    if (playHead >= writeHead) {
        GC::ClearInput();
        return;
    }

    const auto& slot = buf.GetSlot(playHead);

    // 非ロールバック区域では confirmed スロットのみ使用
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
    buf.AdvancePlayHead();
}

} // namespace cccaster::domain::scene
