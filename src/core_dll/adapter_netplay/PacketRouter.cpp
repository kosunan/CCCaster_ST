// ============================================================================
// PacketRouter.cpp — UDP受信パケットのルーティング実装
//
// 【ディスパッチ規則】（優先順）
//
//   1. 統一ヘッダパケット(CC10マジック / 20B+)
//      → ヘッダ解析 → type フィールドで各 Scene にディスパッチ
//
//   3. 旧形式 3バイト シーン同期 (0x20=CS_INPUT, 0x21=LOADING_INPUT, 0x22=REMATCH_MENU)
//      → 後方互換。DummyPeer 旧テストモードとの接続維持用
//
//   4. 旧形式 2バイト入力パケット
//      → OnRemoteInputPacket()（InGame リモート入力バッファへ書き込み）
//
//   5. それ以外 → UNKNOWN ログ
//
// 【統一ヘッダレイアウト (02_packet_specification.md §1)】
//   Offset  Size  Field
//   0x00    4     magic      0x30314343 'CC10'
//   0x04    1     phase      Phase enum
//   0x05    1     type       PacketType enum  ←ルーティングキー
//   0x06    2     sequence
//   0x08    8     timestamp
//   0x10    1     peerState
//   0x11    3     reserved
//   Total: 20 bytes  → ペイロード開始オフセット = 20
//
// 【新パケット種別の追加方法】
//   統一ヘッダ分岐内の switch(type) に case を追加し、
//   ペイロード構造体を memcpy で読み取って Scene API を呼ぶ。
// ============================================================================
#include "core_dll/adapter_netplay/PacketRouter.hpp"
#include "core_dll/adapter_netplay/SyncCoordinator.hpp"

#include "core_dll/session_orchestrator/scene/SceneCharaSelect.hpp"
#include "core_dll/session_orchestrator/scene/SceneLoading.hpp"
#include "core_dll/session_orchestrator/scene/SceneRematch.hpp"
#include "core_dll/session_orchestrator/session/DebugLog.hpp"
#include "core_dll/pure_sync_engine/CentralBuffer.hpp"
#include <cstring>
#include <iostream>

// 前方宣言: SceneRunner.cpp で定義されるリモート入力受信コールバック
// PacketRouter.cpp ローカル型 GameInputEntry を void* 経由で渡す
namespace cccaster::core_dll {
    void OnRemoteInputPacket(uint32_t latestFrame,
                             const void* history, int historyCount);
}

namespace cccaster::core::network {

// ============================================================================
// 統一ヘッダ定数 (仕様書: docs/requirements/dummy_peer_v2/02_packet_specification.md §1)
// ============================================================================

/// CC10マジックナンバー ('CC10' little-endian)
static constexpr uint32_t CC10_MAGIC        = 0x30314343u;

/// 統一ヘッダサイズ [バイト]。ペイロードはこのオフセットから始まる
static constexpr int      UNIFIED_HEADER_SIZE = 20;

/// 統一ヘッダ内 phase フィールドのオフセット
static constexpr int      HDR_PHASE_OFFSET  = 4;

/// 統一ヘッダ内 type フィールドのオフセット
static constexpr int      HDR_TYPE_OFFSET   = 5;

// PacketType値 (仕様書 §3)
static constexpr uint8_t TYPE_CS_INPUT      = 0x10; ///< キャラセレ入力
static constexpr uint8_t TYPE_LOADING_INPUT = 0x20; ///< Loading 画面入力
static constexpr uint8_t TYPE_GAME_INPUT    = 0x40; ///< InGame 対戦入力
static constexpr uint8_t TYPE_REMATCH_MENU  = 0x50; ///< Rematch メニュー選択

// ============================================================================
// ペイロード構造体（ローカル定義 — src/domain/ に触れないため）
// 仕様書 §4.1, §4.2 と一致させること
// ============================================================================
#pragma pack(push, 1)
/// CS_INPUT ペイロード (仕様書 §4.1)
struct CsInputPayload {
    uint32_t frame;
    uint32_t input;   // (direction<<16)|buttons — uint32_t に拡張
};

/// LOADING_INPUT ペイロード (仕様書 §4.2)
struct LoadingInputPayload {
    uint32_t frame;
    uint32_t input;   // (direction<<16)|buttons — uint32_t に拡張
    int32_t  delay;
};

/// REMATCH_MENU ペイロード (仕様書 §4.2)
struct RematchMenuPayload {
    int8_t  menuIndex;
    uint8_t confirmed;
};
/// GAME_INPUT ペイロード (仕様書 §4.1 / DummyPeer UnifiedProtocol.hpp と同一)
struct GameInputEntry {
    uint16_t direction;       // 方向キー (numpad形式)
    uint16_t buttons;         // ボタンビットマスク
};
struct GameInputPayload {
    uint32_t       latestFrame;    // 最新フレーム番号
    uint8_t        inputDelay;     // 入力ディレイ
    uint32_t       roundTimer;     // ラウンドタイマー (Desync検出用)
    uint64_t       wasapiClock;    // WASAPI時刻
    GameInputEntry history[11];    // [0]=最新, [10]=10F前
};
#pragma pack(pop)

// ============================================================================
// PacketRouter::OnPacket — 受信パケットのディスパッチ
// ============================================================================
void PacketRouter::OnPacket(const std::vector<uint8_t>& data, const std::string& fromIp, uint16_t fromPort) {
    if (data.empty()) return;

    // ── SyncCoordinator に全パケットを転送（θ推定 + 疎通チェック用）──
    if (cccaster::core::netplay::SyncCoordinator::GetInstance().IsRunning()) {
        cccaster::core::netplay::SyncCoordinator::GetInstance().OnPacketReceived(data, fromIp, fromPort);
    }

    // ──────────────────────────────────────────────────────────────────────
    // 1. 統一ヘッダ (CC10) パケット
    //
    //    判定: size >= 20 AND data[0..3] == CC10_MAGIC
    //
    //    SYNC_REQ(0x10=9B) との衝突はサイズチェック(>=20)で自動排除される。
    //    統一ヘッダパケットのみがこの分岐に到達する。
    // ──────────────────────────────────────────────────────────────────────
    if (static_cast<int>(data.size()) >= UNIFIED_HEADER_SIZE) {
        uint32_t magic = 0;
        std::memcpy(&magic, data.data(), sizeof(magic));

        if (magic == CC10_MAGIC) {
            // ヘッダからフィールドを読み取る
            const uint8_t phase = data[HDR_PHASE_OFFSET];
            const uint8_t type  = data[HDR_TYPE_OFFSET];
            const int     payloadOffset = UNIFIED_HEADER_SIZE;
            const int     payloadSize   = static_cast<int>(data.size()) - payloadOffset;

            // SyncCoordinator 専用パケットは SyncCoordinator が処理済み
            // ※ PKT_GAME_TICK(0x20) と TYPE_LOADING_INPUT(0x20) が衝突するため
            //    SyncCoordinator 管轄タイプを明示的にスキップする
            if (type == 0x00 ||   // PING
                type == 0x15 ||   // READY
                type == 0x16 ||   // START
                type == 0x20) {   // GAME_TICK ← TYPE_LOADING_INPUT と衝突！
                return;           // SyncCoordinator 側で処理済み
            }

            switch (type) {

            case TYPE_CS_INPUT: {
                // CS_INPUT (0x10): キャラセレ入力
                // ペイロード: CsInputPayload { frame(4), input(2) }
                CsInputPayload pl{};
                if (payloadSize >= static_cast<int>(sizeof(pl))) {
                    std::memcpy(&pl, data.data() + payloadOffset, sizeof(pl));
                }
                cccaster::domain::session::DebugLog(
                    "[PacketRouter] UNIFIED:CS_INPUT phase=0x%02X frame=%u input=0x%04X from=%s:%u",
                    phase, pl.frame, pl.input, fromIp.c_str(), fromPort);
                cccaster::core::sync::CentralBuffer::GetInstance().ConfirmRemote(
                    pl.frame, static_cast<uint32_t>(pl.input));
                break;
            }

            case TYPE_LOADING_INPUT: {
                // LOADING_INPUT (0x20): Loading 画面入力
                // ペイロード: LoadingInputPayload { frame(4), input(2), delay(4) }
                LoadingInputPayload pl{};
                if (payloadSize >= static_cast<int>(sizeof(pl))) {
                    std::memcpy(&pl, data.data() + payloadOffset, sizeof(pl));
                }
                cccaster::domain::session::DebugLog(
                    "[PacketRouter] UNIFIED:LOADING_INPUT phase=0x%02X frame=%u input=0x%04X delay=%d from=%s:%u",
                    phase, pl.frame, pl.input, pl.delay, fromIp.c_str(), fromPort);
                cccaster::core::sync::CentralBuffer::GetInstance().ConfirmRemote(
                    pl.frame, static_cast<uint32_t>(pl.input));
                break;
            }

            case TYPE_REMATCH_MENU: {
                // REMATCH_MENU (0x50): Rematch メニュー選択
                // ペイロード: RematchMenuPayload { menuIndex(1), confirmed(1) }
                RematchMenuPayload pl{};
                if (payloadSize >= static_cast<int>(sizeof(pl))) {
                    std::memcpy(&pl, data.data() + payloadOffset, sizeof(pl));
                }
                cccaster::domain::session::DebugLog(
                    "[PacketRouter] UNIFIED:REMATCH_MENU phase=0x%02X menuIndex=%d confirmed=%u from=%s:%u",
                    phase, static_cast<int>(pl.menuIndex), pl.confirmed, fromIp.c_str(), fromPort);
                cccaster::domain::scene::SceneRematch::SetRemoteRetryMenuIndex(pl.menuIndex);
                break;
            }

            case TYPE_GAME_INPUT: {
                // GAME_INPUT (0x40): InGame 対戦入力
                // ペイロード: GameInputPayload (仕様書 §4.1, 61B)
                GameInputPayload gi{};
                if (payloadSize >= static_cast<int>(sizeof(gi))) {
                    std::memcpy(&gi, data.data() + payloadOffset, sizeof(gi));
                }
                cccaster::domain::session::DebugLog(
                    "[PacketRouter] UNIFIED:GAME_INPUT phase=0x%02X frame=%u btn=0x%04X hist=%d from=%s:%u",
                    phase, gi.latestFrame, gi.history[0].buttons,
                    (payloadSize >= static_cast<int>(sizeof(gi))) ? 11 : 0,
                    fromIp.c_str(), fromPort);
                cccaster::core_dll::OnRemoteInputPacket(gi.latestFrame, gi.history, 11);
                break;
            }

            case 0x00: // PING — SyncCoordinator が処理済み（ログ抑制）
                break;

            case 0x15: // READY — SyncCoordinator が処理済み
                cccaster::domain::session::DebugLog(
                    "[PacketRouter] UNIFIED:READY from=%s:%u",
                    fromIp.c_str(), fromPort);
                break;

            case 0x16: // START — SyncCoordinator が処理済み
                cccaster::domain::session::DebugLog(
                    "[PacketRouter] UNIFIED:START from=%s:%u",
                    fromIp.c_str(), fromPort);
                break;

            default:
                cccaster::domain::session::DebugLog(
                    "[PacketRouter] UNIFIED:UNKNOWN type=0x%02X phase=0x%02X size=%u from=%s:%u",
                    type, phase, (unsigned)data.size(), fromIp.c_str(), fromPort);
                break;
            }
            return;
        }
    }

    // 旧SYNC系パケット (0x10-0x12) は廃止 — θ推定は SyncCoordinator.NetplayClock に統合済み
    const uint8_t type = data[0];

    // ──────────────────────────────────────────────────────────────────────
    // 3. 旧形式 3バイト シーン同期 — 後方互換
    //
    //    DummyPeer --test-mode game 旧バージョン等との接続維持用。
    //    byte[0] = type, byte[1..2] = uint16_t ペイロード
    //
    //   0x20 = CS_INPUT      : キャラセレ入力
    //   0x21 = LOADING_INPUT : ロード画面入力
    //   0x22 = REMATCH_MENU  : リマッチ選択（byte[1] = int8_t 選択インデックス）
    // ──────────────────────────────────────────────────────────────────────
    if (data.size() == 3 && (type == 0x20 || type == 0x21 || type == 0x22)) {
        uint16_t payload = 0;
        std::memcpy(&payload, data.data() + 1, 2);

        switch (type) {
        case 0x20: // 旧 CS_INPUT
            cccaster::domain::session::DebugLog(
                "[PacketRouter] LEGACY:CS_INPUT val=0x%04X from=%s:%u",
                payload, fromIp.c_str(), fromPort);
            cccaster::core::sync::CentralBuffer::GetInstance().ConfirmRemote(
                0, static_cast<uint32_t>(payload));  // 旧形式はフレーム番号なし
            break;

        case 0x21: // 旧 LOADING_INPUT
            cccaster::domain::session::DebugLog(
                "[PacketRouter] LEGACY:LOADING_INPUT val=0x%04X from=%s:%u",
                payload, fromIp.c_str(), fromPort);
            cccaster::core::sync::CentralBuffer::GetInstance().ConfirmRemote(
                0, static_cast<uint32_t>(payload));  // 旧形式はフレーム番号なし
            break;

        case 0x22: // 旧 REMATCH_MENU
            cccaster::domain::session::DebugLog(
                "[PacketRouter] LEGACY:REMATCH_MENU val=%d from=%s:%u",
                static_cast<int>(static_cast<int8_t>(payload & 0xFF)), fromIp.c_str(), fromPort);
            cccaster::domain::scene::SceneRematch::SetRemoteRetryMenuIndex(
                static_cast<int8_t>(payload & 0xFF));
            break;
        }
        return;
    }

    // ──────────────────────────────────────────────────────────────────────
    // 4. 旧形式 2バイト入力パケット → InGame リモート入力
    // ──────────────────────────────────────────────────────────────────────
    if (data.size() == 2) {
        uint16_t remoteInput = 0;
        std::memcpy(&remoteInput, data.data(), 2);
        cccaster::domain::session::DebugLog(
            "[PacketRouter] LEGACY:INPUT val=0x%04X from=%s:%u",
            remoteInput, fromIp.c_str(), fromPort);
        // 旧2B → 単一エントリに変換して queue 経由 (E-12)
        GameInputEntry legacy = { 0, remoteInput };
        cccaster::core_dll::OnRemoteInputPacket(0, &legacy, 1);
        return;
    }

    // ──────────────────────────────────────────────────────────────────────
    // 5. 未知パケット
    // ──────────────────────────────────────────────────────────────────────
    cccaster::domain::session::DebugLog(
        "[PacketRouter] UNKNOWN packet: size=%u type=0x%02X from=%s:%u",
        (unsigned)data.size(), type, fromIp.c_str(), fromPort);
}

} // namespace cccaster::core::network
