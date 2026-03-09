#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace dummy_peer {

// =============================================================
//  統一パケットプロトコル定義 v2
//  CCCaster_v10 のフェーズ別通信に対応する共通ヘッダ + ペイロード
//
// 【v2 変更点】
//   - UnifiedPacketHeader を 16B → 20B に拡張 (peerState + reserved[3])
//   - PeerState enum class を追加 (8ステート)
//   - PacketType に LOADING_INPUT(0x20), REMATCH_MENU(0x50), STATE_REPORT(0xE0) 追加
//   - LoadingInputPayload / RematchMenuPayload / StateReportPayload 追加
//   - MakeHeader() に peerState 引数追加
//   - ParseHeader() の size チェックを 16B 後方互換対応
// =============================================================

// マジックナンバー 'CC10' (リトルエンディアン)
constexpr uint32_t PACKET_MAGIC = 0x30314343; // "CC10"

// ================================================================
// フェーズ定義
// ================================================================
enum class Phase : uint8_t {
    NEGOTIATION   = 0x00,
    CHARA_SELECT  = 0x01,
    LOADING       = 0x02,
    PRE_GAME_SYNC = 0x03,
    IN_GAME       = 0x04,
    REMATCH       = 0x05,
};

// ================================================================
// PeerState — 業務状態変数 (v2 NEW)
//
// パケットヘッダの peerState フィールドに搭載する。
// DLL 本体の各画面フェーズと 1:1 で対応する。
// ================================================================
enum class PeerState : uint8_t {
    BOOTING         = 0x01,  ///< 起動〜50F高速スキップ終了
    CS_SYNC_WAIT    = 0x21,  ///< キャラセレ：TimeSynchronizer 待ち
    CS_SYNC_DONE    = 0x22,  ///< キャラセレ：TimeSync完了・ClockOffset適用済み
    CS_SELECTING    = 0x23,  ///< キャラ/ムーン/カラー 選択中
    CS_STAGE_SELECT = 0x24,  ///< ステージ選択中
    LOADING         = 0x30,  ///< ロード画面
    IN_GAME         = 0x40,  ///< 対戦中
    REMATCH         = 0x50,  ///< リマッチ画面
};

// ================================================================
// パケット種別
// ================================================================
enum class PacketType : uint8_t {
    // Negotiation (Phase 0) — SessionNegotiator 互換は NegotiationResponder で処理
    NEGO_PING      = 0x00,

    // CharaSelect Sync (Phase 1.5)
    CS_SYNC_READY  = 0x08,

    // CharaSelect Input (Phase 2)
    CS_INPUT       = 0x10,

    // TimeSynchronizer 互換 (直接形式: SyncResponder)
    TIME_SYNC_REQ  = 0x31,
    TIME_SYNC_RES  = 0x32,

    // Pre-Game Sync (Phase 3/4)
    RNG_SYNC       = 0x30,
    READY          = 0x33,

    // InGame (Phase 4+)
    GAME_INPUT     = 0x40,

    // Loading 入力 (v2 NEW)
    LOADING_INPUT  = 0x20,

    // Rematch メニュー選択 (v2 NEW)
    REMATCH_MENU   = 0x50,

    // 業務状態レポート (v2 NEW)
    STATE_REPORT   = 0xE0,

    // Common
    HEARTBEAT      = 0xF0,
    DISCONNECT     = 0xFF,
};

// ================================================================
// 統一パケットヘッダ v2 (20 bytes)
//
// 旧ヘッダ (16B) との後方互換:
//   - size >= 16 であれば受理、peerState = 0x00 として処理
//   - 新ヘッダ (20B) → 旧DLL: 追加4バイトは無視される
// ================================================================
#pragma pack(push, 1)
struct UnifiedPacketHeader {
    uint32_t magic;       // PACKET_MAGIC ('CC10')
    uint8_t  phase;       // Phase enum
    uint8_t  type;        // PacketType enum
    uint16_t sequence;    // シーケンス番号
    uint64_t timestamp;   // 送信時刻 (us, system_clock)
    uint8_t  peerState;   // [v2 NEW] PeerState enum (旧受信時は 0x00)
    uint8_t  reserved[3]; // アライメント予約
};
static_assert(sizeof(UnifiedPacketHeader) == 20, "Header must be 20 bytes");

// ================================================================
// ペイロード定義
// ================================================================

// CS_SYNC_READY: 200F到達通知
struct CsSyncReadyPayload {
    uint32_t frameCount;  // 到達フレーム数 (通常200)
};

// CS_INPUT: キャラセレ入力
struct CsInputPayload {
    uint32_t frame;       // フレーム番号
    uint16_t input;       // 入力値 (方向+ボタン)
};

// TIME_SYNC_REQ: NTP方式時刻同期リクエスト
struct TimeSyncReqPayload {
    int64_t t1;           // リクエスタ送信時刻
};

// TIME_SYNC_RES: NTP方式時刻同期レスポンス
struct TimeSyncResPayload {
    int64_t t1;           // エコーバック
    int64_t t2;           // レスポンダ受信時刻
    int64_t t3;           // レスポンダ送信時刻
};

// RNG_SYNC: 乱数シード同期
struct RngSyncPayload {
    uint32_t rngState0;
    uint32_t rngState1;
    uint32_t rngState2;
    uint8_t  rngArray[220];
};

// READY: 準備完了
struct ReadyPayload {
    uint8_t ready;        // 1 = ready
};

// GAME_INPUT: 冗長化対戦入力 (11フレーム)
struct GameInputEntry {
    uint16_t direction;   // 方向キー (numpad形式)
    uint16_t buttons;     // ボタンビットマスク
};
struct GameInputPayload {
    uint32_t      latestFrame;
    uint8_t       inputDelay;
    uint32_t      roundTimer;   // Desync検出用
    uint64_t      wasapiClock;  // WASAPI時刻
    GameInputEntry history[11]; // [0]=最新, [10]=10F前
};

// LOADING_INPUT: Loading画面ディレイ入力 (v2 NEW)
struct LoadingInputPayload {
    uint32_t frame;          // フレーム番号
    uint16_t input;          // 入力値
    int32_t  delay;          // 算出済みディレイ値
};

// REMATCH_MENU: Rematch メニュー選択 (v2 NEW)
struct RematchMenuPayload {
    int8_t  menuIndex;       // 0=Rematch(再戦), 1=CharaSelect(キャラセレ戻り)
    uint8_t confirmed;       // 1=確定済み
};

// STATE_REPORT: 業務状態レポート (v2 NEW)
struct StateReportPayload {
    uint8_t  peerState;          // PeerState の値
    uint32_t framesInState;      // 現ステートの経過フレーム数
    uint16_t configDelay;        // 設定ディレイ値
    uint8_t  configMaxRollback;  // 設定ロールバック値
    int64_t  clockOffsetUs;      // 現在のクロックオフセット (μs)
    int64_t  rttUs;              // 最新RTT (μs)
};

#pragma pack(pop)

// ================================================================
// ユーティリティ関数
// ================================================================

/// 統一パケットヘッダを生成 (v2: peerState 引数追加)
inline UnifiedPacketHeader MakeHeader(Phase phase, PacketType type, uint16_t seq,
                                       uint64_t timestampUs,
                                       PeerState peerState = PeerState::BOOTING) {
    UnifiedPacketHeader h{};
    h.magic     = PACKET_MAGIC;
    h.phase     = static_cast<uint8_t>(phase);
    h.type      = static_cast<uint8_t>(type);
    h.sequence  = seq;
    h.timestamp = timestampUs;
    h.peerState = static_cast<uint8_t>(peerState);
    h.reserved[0] = h.reserved[1] = h.reserved[2] = 0;
    return h;
}

/// ヘッダ + ペイロードからパケットバイト列を生成
template<typename PayloadT>
inline std::vector<uint8_t> BuildPacket(Phase phase, PacketType type, uint16_t seq,
                                         uint64_t timestampUs, const PayloadT& payload,
                                         PeerState peerState = PeerState::BOOTING) {
    UnifiedPacketHeader hdr = MakeHeader(phase, type, seq, timestampUs, peerState);
    std::vector<uint8_t> pkt(sizeof(hdr) + sizeof(payload));
    std::memcpy(pkt.data(), &hdr, sizeof(hdr));
    std::memcpy(pkt.data() + sizeof(hdr), &payload, sizeof(payload));
    return pkt;
}

/// ヘッダのみのパケットを生成 (ペイロードなし)
inline std::vector<uint8_t> BuildHeaderOnlyPacket(Phase phase, PacketType type, uint16_t seq,
                                                    uint64_t timestampUs,
                                                    PeerState peerState = PeerState::BOOTING) {
    UnifiedPacketHeader hdr = MakeHeader(phase, type, seq, timestampUs, peerState);
    std::vector<uint8_t> pkt(sizeof(hdr));
    std::memcpy(pkt.data(), &hdr, sizeof(hdr));
    return pkt;
}

/// 受信バッファからヘッダを解析 (バリデーション付き)
/// v2 後方互換: 旧16Bヘッダも受理（peerState=0として扱う）
inline bool ParseHeader(const uint8_t* data, int len, UnifiedPacketHeader& out) {
    // 旧16Bヘッダ互換: 16B以上あれば受理
    constexpr int LEGACY_HEADER_SIZE = 16;
    if (len < LEGACY_HEADER_SIZE) return false;

    // まず16Bだけコピーして magic を確認
    std::memset(&out, 0, sizeof(out));
    int copyBytes = (len >= static_cast<int>(sizeof(out)))
                    ? static_cast<int>(sizeof(out))
                    : LEGACY_HEADER_SIZE;
    std::memcpy(&out, data, copyBytes);
    return out.magic == PACKET_MAGIC;
}

/// ヘッダに続くペイロードを解析
template<typename PayloadT>
inline bool ParsePayload(const uint8_t* data, int len, PayloadT& out) {
    int offset = sizeof(UnifiedPacketHeader);
    if (len < offset + static_cast<int>(sizeof(PayloadT))) return false;
    std::memcpy(&out, data + offset, sizeof(PayloadT));
    return true;
}

} // namespace dummy_peer
