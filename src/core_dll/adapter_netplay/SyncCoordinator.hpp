#pragma once
// ============================================================================
// SyncCoordinator — ネットプレイ通信同期の統括
//
// 【設計思想】
//   通信スレッドがティックマスターとして機能し、
//   WASAPIクロックベースの高精度フレームカウントを管理する。
//   DLLスレッド（ゲームスレッド）はSharedSyncStateを参照するだけ。
//
// 【2レイヤー分離】
//   - NetplayClock (純粋関数群): θ推定、ドリフト補正、1F周期算出
//   - SyncCoordinator (本クラス): モード遷移ステートマシン + パケットI/O
//
// 【モード遷移】
//   WaitReady  → (双方READY) → WaitStart → (合意時刻到達) → Counting
//
// 【スレッド間ルール】
//   - 同期フラグの書き込み権限は通信スレッドのみ。
//   - DLLスレッドは Read-only で参照。
//   - DLLスレッドが書き込むのは localInputQueue への Push のみ。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include "core_dll/adapter_netplay/timer/NetplayClock.hpp"

namespace cccaster {
namespace core {
namespace netplay {

// ============================================================================
// SyncMode — 通信スレッドの状態
// ============================================================================
enum class SyncMode {
    WaitReady,   // 準備完了待機 — READY信号を送り、相手のREADYを待つ
    WaitStart,   // 開始時刻待機 — θ推定→START送受信→合意時刻到達を待つ
    Counting     // フレームカウント中 — 1F周期でカウントアップ
};

// ============================================================================
// SharedSyncState — スレッド間共有データ
// ============================================================================
struct SharedSyncState {
    // ─── ティックマスター ───────────────────────────────
    std::atomic<uint32_t> currentFrame{0};
    std::atomic<int64_t>  currentTickUs{16666};

    // ─── 同期状態フラグ ─────────────────────────────────
    std::atomic<bool>     isSynced{false};     // 対戦開始可能
    std::atomic<bool>     isPeerAlive{false};   // 疎通確認
    std::atomic<bool>     peerReady{false};     // 相手のREADY受信済み
    std::atomic<int64_t>  clockOffsetUs{0};     // θ
    std::atomic<int64_t>  lastRttUs{0};

    // ─── リモート入力リングバッファ ──────────────────────
    static constexpr int RING_SIZE = 20;
    struct InputSlot {
        std::atomic<uint32_t> frame{0};
        std::atomic<uint32_t> input{0};
    };
    InputSlot remoteInputs[RING_SIZE];
    std::atomic<int> remoteWriteIndex{0};
};

// ============================================================================
// ローカル入力エントリ / 受信パケットエントリ
// ============================================================================
struct LocalInputEntry {
    uint32_t frame;
    uint32_t input;
};

struct ReceivedPacket {
    std::vector<uint8_t> data;
    std::string fromIp;
    uint16_t fromPort;
    int64_t  receiveTimeUs;
};

// ============================================================================
// SyncCoordinator 本体
// ============================================================================
class SyncCoordinator {
public:
    static SyncCoordinator& GetInstance();
    static const SharedSyncState& GetState() {
        return GetInstance()._state;
    }
    static SharedSyncState& GetMutableState() {
        return GetInstance()._state;
    }

    // ─── ライフサイクル ─────────────────────────────────
    void Start(bool isHost,
               const std::string& targetIp, uint16_t targetPort,
               uint16_t localPort,
               int delayFrames, int maxRollback);
    void Stop();
    bool IsRunning() const { return _running.load(); }

    // ─── 時計データ読取り（オーバーレイ用）───────────────
    int64_t GetRttUs() const { return _clock.GetRttUs(); }
    int64_t GetThetaUs() const { return _clock.GetThetaUs(); }
    int64_t GetBaselineTheta() const { return _clock.GetBaselineTheta(); }

    // ─── 受信パケットキュー ─────────────────────────────
    void OnPacketReceived(const std::vector<uint8_t>& data,
                          const std::string& fromIp, uint16_t fromPort);

    // ─── DLLスレッドからの入力送信 ──────────────────────
    // PushLocalInput 廃止 — 通信スレッドが DirectInputHook から直接読取り

    // ─── 定数 ──────────────────────────────────────────
    static constexpr int     MAX_SENDS_PER_FRAME    = 3;
    static constexpr int     SUB_TICKS_PER_FRAME    = 3;        // 1Fを3分割
    static constexpr int64_t START_MARGIN_US        = 500000;   // 500ms
    static constexpr int     DISCONNECT_TIMEOUT_FRAMES = 180;   // 180F = 3秒

    // 統一ヘッダ
    static constexpr int     HDR_TIMESTAMP_OFFSET   = 8;
    static constexpr int     UNIFIED_HEADER_SIZE    = 20;
    static constexpr uint32_t CC10_MAGIC            = 0x30314343u;

    // パケットタイプ
    static constexpr uint8_t PKT_READY     = 0x15;  // 準備完了信号
    static constexpr uint8_t PKT_START     = 0x16;  // スタート時刻通知
    static constexpr uint8_t PKT_GAME_TICK = 0x20;  // ゲームティック（入力+タイミング）

private:
    SyncCoordinator() = default;
    ~SyncCoordinator() { Stop(); }
    SyncCoordinator(const SyncCoordinator&) = delete;
    SyncCoordinator& operator=(const SyncCoordinator&) = delete;

    // ─── 通信スレッド ──────────────────────────────────
    void ThreadMain();
    void DrainAndProcessPackets();
    void SendPacket(const std::vector<uint8_t>& data);
    void SendPing();
    void SendReady();
    void SendStart(int64_t startTimeUs);
    void SendGameTick(uint32_t frame, uint32_t localInput);
    static void SleepUntil(int64_t targetUs);

    // ─── 状態 ──────────────────────────────────────────
    SharedSyncState _state;
    std::atomic<bool> _running{false};
    std::thread _thread;
    SyncMode _mode = SyncMode::WaitReady;

    // ─── 計算エンジン ──────────────────────────────────
    timer::NetplayClock _clock;

    // ─── 構成 ──────────────────────────────────────────
    bool     _isHost = false;
    std::string _targetIp;
    uint16_t _targetPort = 0;
    uint16_t _localPort  = 0;
    int      _delayFrames = 0;
    int      _maxRollback = 0;

    // ─── 受信パケットキュー ─────────────────────────────
    std::mutex _recvMutex;
    std::vector<ReceivedPacket> _recvQueue;
    std::vector<ReceivedPacket> _recvQueueSwap;

    // ─── 現フレームの確定入力（3サブティックで同一内容を送信）───
    uint32_t _lastLocalInput = 0;  // (direction<<16)|buttons

    // ─── NTP T1-T4 エコー追跡 ─────────────────────────
    int64_t _lastPeerT1     = 0;  // 最後に受信した相手パケットの t_send（= 相手の T1）
    int64_t _lastPeerRecvUs = 0;  // そのパケットを受信した自分の WASAPI 時刻（= T2）

    // ─── 内部タイマー ──────────────────────────────────
    int64_t _lastRecvUs = 0;
    bool    _peerReady = false;
    bool    _startSent = false;
    int     _framesSinceLastRecv = 0;  // 疎通カウンタ
    uint32_t _latestPeerFrame = 0;     // 最後に受信した相手の baseFrame（キャッチアップ用）

    // ─── 実ピアポート（NAT越え用）──────────────────────
    uint16_t _peerActualPort = 0;
};

} // namespace netplay
} // namespace core
} // namespace cccaster
