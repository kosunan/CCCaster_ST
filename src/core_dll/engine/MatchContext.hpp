#pragma once
// ============================================================================
// MatchContext — 画面間で持ち回す業務変数（POD構造体）
// 
// ★ dllmain.cpp の InitThread で IPC から構築され、
//   SceneRunner::Step() で参照されて全Sceneで共有される
//
// 重いオブジェクト（RollbackEngine, FrameInputBuffer 等）は
// 各モジュールの static に配置し、Scene関数にはポインタ渡しする
// ============================================================================

#include <cstdint>

namespace cccaster::domain::session {

struct MatchContext {
    // ---- 起動時確定（不変）----
    uint8_t  appMode      = 0;    // 0=Versus, 1=Training, 2=Spectator (IpcGameModeと一致)
    bool     isHost       = false;
    uint8_t  _pad0        = 0;

    // ---- ネットワーク接続先 ----
    char     peerIp[64]   = {};    // 接続先IP（null終端）
    uint16_t peerPort     = 0;     // 接続先ポート
    uint16_t localPort    = 0;     // 自バインドポート (Host=指定, Client=OS割当済み)

    // ---- 同期パラメータ ----
    int16_t  delay        = 2;    // 共有ディレイ (default: 2F — CLI DefaultDelay と一致)
    int16_t  maxRollback  = 4;    // 最大ロールバック深度 (default: 4F — CLI MaxRollback と一致)

    // ---- フェーズ進行フラグ ----
    bool     charaSelectSyncDone  = false;  // キャラセレ時刻同期完了
    bool     roundStartSynced     = false;  // ラウンド開始同期完了
    bool     fastBoot             = false;  // 高速起動中（Scene処理スキップ）
    bool     rollbackReady        = false;  // RollbackEngine始動済み

    // ---- フレームカウンタ ----
    uint32_t framesInPhase       = 0;   // 各画面に入ってからの単純な経過フレーム
    uint32_t phaseBaseWorldTimer = 0;   // フェーズ開始時の基準ワールドタイム（WT）。ここから引算で「相対フレーム」を算出
};

} // namespace cccaster::domain::session

