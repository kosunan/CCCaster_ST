#pragma once
// ============================================================================
// NetplaySession — 通信スレッド統括
//
// 【責務】
//   パケットの送受信に専念する。
//   パケット解析・Θ計算・α補正・FrameInputBuffer操作は GameTickCodec に委譲。
//   フレームリズム生成は Metronome に委譲。
//
// 【モード遷移】
//   WaitReady  → (双方READY) → WaitStart → (θ安定+合意時刻到達) → Counting
//
// 【スレッド間ルール】
//   - 通信スレッドは送受信と GameTickCodec 呼出しのみ。
//   - Metronome は独立スレッドでカウンタをカウントアップ。
//   - DLLスレッド（ゲームスレッド）は FrameInputBuffer を監視するだけ。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include "core_dll/network/GameTickCodec.hpp"
#include "core_dll/timing/Metronome.hpp"

namespace cccaster {
namespace core {
namespace netplay {

// ============================================================================
// SyncMode — 通信スレッドの状態
// ============================================================================
enum class SyncMode {
    WaitReady,   // 準備完了待機 — READY信号を送り、相手のREADYを待つ
    WaitStart,   // 開始時刻待機 — θ推定→START送受信→合意時刻到達を待つ
    Counting     // フレームカウント中 — パケット送受信 + FrameInputBuffer書込み
};

// ============================================================================
// SharedSyncState — スレッド間共有データ
// ============================================================================
struct SharedSyncState {
    // ─── ティックマスター ───────────────────────────────
    std::atomic<uint32_t> currentFrame{0};
    std::atomic<int64_t>  currentTickUs{16666};

    // ─── 同期状態フラグ ─────────────────────────────────
    std::atomic<bool>     isSynced{false};
    std::atomic<bool>     isPeerAlive{false};
    std::atomic<bool>     peerReady{false};
    std::atomic<int64_t>  clockOffsetUs{0};
    std::atomic<int64_t>  lastRttUs{0};

    // ─── IntroBarrier（ゲーム↔通信スレッド間）───────────────
    std::atomic<bool>     localIntroComplete{false};  // ゲームスレッドが設定
    std::atomic<bool>     peerIntroComplete{false};    // 通信スレッドが設定（受信時）

    // ─── Keepalive 要求（ゲーム→通信スレッド）─────────────────
    std::atomic<bool>     needKeepalive{true};         // CB書込みしないPhaseで true

    // ─── Rematch メニュー選択（ゲーム↔通信スレッド間）────────
    std::atomic<int8_t>   localRetryMenuIndex{-1};     // ゲームスレッドが設定, -1=未決定

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
// 受信パケットエントリ
// ============================================================================
struct ReceivedPacket {
    std::vector<uint8_t> data;
    std::string fromIp;
    uint16_t fromPort;
    int64_t  receiveTimeUs;
};

// ============================================================================
// NetplaySession 本体 — 通信専用
// ============================================================================
class NetplaySession {
public:
    static NetplaySession& GetInstance();
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

    /// @brief メトロノームへのアクセサ（DLLスレッドから WaitForNextTick 用）
    Metronome& GetMetronome() { return _metronome; }

    // ─── 時計データ読取り（オーバーレイ用、GameTickCodec 委譲）──
    int64_t GetRttUs() const        { return _calc.GetRttUs(); }
    int64_t GetThetaUs() const      { return _calc.GetThetaUs(); }
    int64_t GetBaselineTheta() const { return _calc.GetBaselineTheta(); }
    uint32_t GetLatestPeerFrame() const { return _calc.GetLatestPeerFrame(); }

    // ─── D/R 動的変更（GameTickCodec 委譲）─────────────
    void SetDelayFrames(int d)  { _calc.SetDelayFrames(d); }
    void SetMaxRollback(int r)  { _calc.SetMaxRollback(r); }

    // ─── 受信パケットキュー ─────────────────────────────
    void OnPacketReceived(const std::vector<uint8_t>& data,
                          const std::string& fromIp, uint16_t fromPort);

    // ─── 定数 ──────────────────────────────────────────
    static constexpr int     KEEPALIVE_INTERVAL_FRAMES = 3;  // 3フレーム(≈50ms)ごとに keepalive
    static constexpr int64_t START_MARGIN_US            = 500000;

    // パケット定数
    static constexpr int      UNIFIED_HEADER_SIZE   = GameTickCodec::UNIFIED_HEADER_SIZE;
    static constexpr uint32_t CC10_MAGIC            = GameTickCodec::CC10_MAGIC;
    static constexpr uint8_t  PKT_GAME_TICK         = GameTickCodec::PKT_GAME_TICK;

private:
    NetplaySession() = default;
    ~NetplaySession() { Stop(); }
    NetplaySession(const NetplaySession&) = delete;
    NetplaySession& operator=(const NetplaySession&) = delete;

    // ─── 通信スレッド ──────────────────────────────────
    void ThreadMain();
    void DrainAndProcessPackets();
    void SendPacket(const std::vector<uint8_t>& data);
    static void SleepUntil(int64_t targetUs);

    // ─── 状態 ──────────────────────────────────────────
    SharedSyncState _state;
    std::atomic<bool> _running{false};
    std::thread _thread;
    SyncMode _mode = SyncMode::WaitReady;

    // ─── 委譲先 ────────────────────────────────────────
    GameTickCodec _calc;
    Metronome _metronome;

    // ─── 構成 ──────────────────────────────────────────
    bool     _isHost = false;
    std::string _targetIp;
    uint16_t _targetPort = 0;
    uint16_t _localPort  = 0;
    bool     _startSent = false;

    // ─── 受信パケットキュー ─────────────────────────────
    std::mutex _recvMutex;
    std::vector<ReceivedPacket> _recvQueue;
    std::vector<ReceivedPacket> _recvQueueSwap;

    // ─── CB writeHead 監視用 ──────────────────────────
    uint32_t _lastSentFrame = 0;
    uint32_t _lastLogFrame  = 0;
    int      _keepaliveCounter = 0;

    // ─── 実ピアポート（NAT越え用）──────────────────────
    uint16_t _peerActualPort = 0;
};

} // namespace netplay
} // namespace core
} // namespace cccaster
