// ============================================================================
// SyncCoordinator.cpp — ネットプレイ通信同期の統括（実装）
//
// 【モード遷移ステートマシン】
//   WaitReady  → (双方READY) → WaitStart → (合意時刻到達) → Counting
//
// 【パケット送受信】
//   READY (0x15): 準備完了信号
//   START (0x16): スタート時刻通知（WASAPIクロック値）
//   PING  (CC10ヘッダ): タイムスタンプ付きハートビート
//
// 【計算エンジン分離】
//   θ推定・ドリフト補正・1F周期算出は NetplayClock に委譲。
//   本クラスはモード遷移とパケットI/Oのみ。
// ============================================================================

#include "core_dll/adapter_netplay/SyncCoordinator.hpp"
#include "core_dll/adapter_netplay/timer/WasapiClock.hpp"
#include "core_dll/adapter_netplay/NetplayManager.hpp"
#include "core_dll/session_orchestrator/session/DebugLog.hpp"
#include <algorithm>
#include <cstring>
#include <windows.h>

namespace cccaster {
namespace core {
namespace netplay {

// ============================================================================
// パケットペイロード構造体（統一プロトコル実形式）
// ============================================================================
#pragma pack(push, 1)
/// READY ペイロード: 空（ヘッダのみで十分）
// type=0x15, ペイロードなし

/// START ペイロード: スタート時刻 (8bytes)
struct StartPayload {
    int64_t startTimeUs;  // WASAPIクロックによる開始時刻 (μs)
};

/// PING ペイロード: 空（ヘッダ内タイムスタンプで十分）
// type=0x00, ペイロードなし
#pragma pack(pop)

// ============================================================================
// BuildUnifiedPacket — CC10統一ヘッダ + ペイロードを組み立てる
// ============================================================================
static std::vector<uint8_t> BuildUnifiedPacket(
    uint8_t phase, uint8_t type,
    int64_t timestampUs,
    const void* payload = nullptr, size_t payloadSize = 0)
{
    std::vector<uint8_t> pkt(SyncCoordinator::UNIFIED_HEADER_SIZE + payloadSize, 0);
    uint32_t magic = SyncCoordinator::CC10_MAGIC;
    std::memcpy(pkt.data(), &magic, sizeof(magic));
    pkt[4] = phase;
    pkt[5] = type;
    std::memcpy(pkt.data() + SyncCoordinator::HDR_TIMESTAMP_OFFSET,
                &timestampUs, sizeof(timestampUs));
    if (payload && payloadSize > 0) {
        std::memcpy(pkt.data() + SyncCoordinator::UNIFIED_HEADER_SIZE,
                    payload, payloadSize);
    }
    return pkt;
}

// ─── シングルトン ──────────────────────────────────────
SyncCoordinator& SyncCoordinator::GetInstance() {
    static SyncCoordinator instance;
    return instance;
}

// ============================================================================
// Start — 通信スレッドを起動する
// ============================================================================
void SyncCoordinator::Start(bool isHost,
                            const std::string& targetIp, uint16_t targetPort,
                            uint16_t localPort,
                            int delayFrames, int maxRollback) {
    if (_running.load()) return;

    _isHost       = isHost;
    _targetIp     = targetIp;
    _targetPort   = targetPort;
    _localPort    = localPort;
    _delayFrames  = delayFrames;
    _maxRollback  = maxRollback;

    // SharedSyncState リセット
    _state.currentFrame.store(0);
    _state.currentTickUs.store(timer::NetplayClock::BASE_TICK_US);
    _state.isSynced.store(false);
    _state.isPeerAlive.store(false);
    _state.peerReady.store(false);
    _state.clockOffsetUs.store(0);
    _state.lastRttUs.store(0);
    _state.remoteWriteIndex.store(0);
    for (int i = 0; i < SharedSyncState::RING_SIZE; i++) {
        _state.remoteInputs[i].frame.store(0);
        _state.remoteInputs[i].input.store(0);
    }

    // 内部状態リセット
    _clock.Reset();
    _mode = SyncMode::WaitReady;
    _peerReady = false;
    _startSent = false;
    _framesSinceLastRecv = 0;
    _peerActualPort = 0;
    _lastRecvUs = timer::WasapiClock::GetTimeUs();

    // キュークリア
    {
        std::lock_guard<std::mutex> lock(_recvMutex);
        _recvQueue.clear();
        _recvQueueSwap.clear();
    }
    {
        std::lock_guard<std::mutex> lock(_localMutex);
        _localQueue.clear();
        _localQueueSwap.clear();
    }

    cccaster::domain::session::DebugLog(
        "[SyncCoordinator] Starting. host=%d target=%s:%u localPort=%u delay=%d maxRB=%d",
        isHost, targetIp.c_str(), targetPort, localPort, delayFrames, maxRollback);

    _running.store(true);
    _thread = std::thread(&SyncCoordinator::ThreadMain, this);
}

// ============================================================================
// Stop — 通信スレッドを停止する
// ============================================================================
void SyncCoordinator::Stop() {
    if (!_running.load()) return;
    _running.store(false);
    if (_thread.joinable()) {
        _thread.join();
    }
    cccaster::domain::session::DebugLog("[SyncCoordinator] Stopped.");
}

// ============================================================================
// OnPacketReceived — 受信スレッドから呼ばれ、キューに Push するだけ
// ============================================================================
void SyncCoordinator::OnPacketReceived(const std::vector<uint8_t>& data,
                                        const std::string& fromIp, uint16_t fromPort) {
    int64_t receiveTime = timer::WasapiClock::GetTimeUs();
    std::lock_guard<std::mutex> lock(_recvMutex);
    _recvQueue.push_back({data, fromIp, fromPort, receiveTime});
}

// ============================================================================
// PushLocalInput — DLLスレッドからローカル入力を送信キューに追加
// ============================================================================
void SyncCoordinator::PushLocalInput(const LocalInputEntry& entry) {
    std::lock_guard<std::mutex> lock(_localMutex);
    _localQueue.push_back(entry);
}

// ============================================================================
// DrainAndProcessPackets — 受信キューを drain して処理
// ============================================================================
void SyncCoordinator::DrainAndProcessPackets() {
    {
        std::lock_guard<std::mutex> lock(_recvMutex);
        std::swap(_recvQueue, _recvQueueSwap);
    }

    for (const auto& pkt : _recvQueueSwap) {
        // 疎通更新
        _lastRecvUs = pkt.receiveTimeUs;
        _state.isPeerAlive.store(true, std::memory_order_release);
        _framesSinceLastRecv = 0;

        // 実ピアポートを記録（NAT越え用）
        _peerActualPort = pkt.fromPort;

        // ── 統一ヘッダ (CC10) パケットのみ処理 ──
        if (static_cast<int>(pkt.data.size()) < UNIFIED_HEADER_SIZE) continue;

        uint32_t magic = 0;
        std::memcpy(&magic, pkt.data.data(), sizeof(magic));
        if (magic != CC10_MAGIC) continue;

        uint8_t pktType = pkt.data[5]; // type フィールド

        // ── READY パケット (type=0x15) ──
        if (pktType == PKT_READY) {
            if (!_peerReady) {
                _peerReady = true;
                _state.peerReady.store(true, std::memory_order_release);
                cccaster::domain::session::DebugLog("[SyncCoordinator] Received READY from peer.");
            }
            continue;
        }

        // ── START パケット (type=0x16, ペイロードに8bytes startTime) ──
        if (pktType == PKT_START && pkt.data.size() >= UNIFIED_HEADER_SIZE + sizeof(StartPayload)) {
            StartPayload sp{};
            std::memcpy(&sp, pkt.data.data() + UNIFIED_HEADER_SIZE, sizeof(sp));
            _clock.SetPeerStartTime(sp.startTimeUs);
            cccaster::domain::session::DebugLog(
                "[SyncCoordinator] Received START from peer. peerStartTime=%lld us", sp.startTimeUs);
            continue;
        }

        // ── PING / その他: タイムスタンプからθ推定 ──
        int64_t peerTimestamp = 0;
        std::memcpy(&peerTimestamp, pkt.data.data() + HDR_TIMESTAMP_OFFSET, sizeof(peerTimestamp));
        if (peerTimestamp > 0) {
            _clock.AddThetaSample(peerTimestamp, pkt.receiveTimeUs);
            _state.clockOffsetUs.store(_clock.GetThetaUs(), std::memory_order_release);
        }
    }

    _recvQueueSwap.clear();
}

// ============================================================================
// SendPacket — NetplayManager の送信関数経由でパケットを送る
//
// CS_INPUT 等と同一の送信先を使うため、NetplayManager::GetSendFunc() を利用。
// SyncCoordinator 独自の _targetIp/_targetPort は使わない。
// ============================================================================
void SyncCoordinator::SendPacket(const std::vector<uint8_t>& data) {
    auto sendFunc = cccaster::netplay::NetplayManager::GetInstance().GetSendFunc();
    if (sendFunc) {
        sendFunc(data);
    }
}

// ============================================================================
// SendPing — PING パケット送信（CC10ヘッダ type=0x00, ペイロードなし）
// ============================================================================
void SyncCoordinator::SendPing() {
    int64_t now = timer::WasapiClock::GetTimeUs();
    auto pkt = BuildUnifiedPacket(0x00, 0x00, now);
    SendPacket(pkt);
}

// ============================================================================
// SendReady — READY 信号送信（CC10ヘッダ type=0x15, ペイロードなし）
// ============================================================================
void SyncCoordinator::SendReady() {
    int64_t now = timer::WasapiClock::GetTimeUs();
    auto pkt = BuildUnifiedPacket(0x00, PKT_READY, now);
    SendPacket(pkt);
}

// ============================================================================
// SendStart — START 信号送信（CC10ヘッダ type=0x16 + StartPayload）
// ============================================================================
void SyncCoordinator::SendStart(int64_t startTimeUs) {
    int64_t now = timer::WasapiClock::GetTimeUs();
    StartPayload sp{};
    sp.startTimeUs = startTimeUs;
    auto pkt = BuildUnifiedPacket(0x00, PKT_START, now, &sp, sizeof(sp));
    SendPacket(pkt);

    cccaster::domain::session::DebugLog(
        "[SyncCoordinator] Sent START. startTime=%lld us", startTimeUs);
}

// ============================================================================
// SleepUntil — 精密スリープ（Sleep + スピンウェイトのハイブリッド）
// ============================================================================
//
// 2ms 以上残り → Sleep(1) で CPU 節約
// 2ms 未満     → スピンウェイトで精度優先
//
void SyncCoordinator::SleepUntil(int64_t targetUs) {
    while (true) {
        int64_t remain = targetUs - timer::WasapiClock::GetTimeUs();
        if (remain <= 0) break;
        if (remain > 2000) {
            Sleep(1);
        } else {
            // スピンウェイト（CPU省電力ヒント付き）
            YieldProcessor();
        }
    }
}

// ============================================================================
// ThreadMain — 通信スレッドのメインループ（サブティック3分割方式）
// ============================================================================
//
// 【設計】
//   1Fの基礎時間（tickUs ≈ 16666μs）を SUB_TICKS_PER_FRAME (=3) 分割。
//   ループは ~5555μs 間隔で回り、3サブティック目でフレームを進行させる。
//
//   WaitReady/WaitStart: 毎サブティックで READY/PING を送信（旧200ms→~5.5ms）
//   Counting: subTickIndex == 0 のサブティックでフレーム進行
//
// ============================================================================
void SyncCoordinator::ThreadMain() {
    cccaster::domain::session::DebugLog("[SyncCoordinator] Thread started. Mode=WaitReady");

    int64_t tickUs     = timer::NetplayClock::BASE_TICK_US;
    int64_t subTickUs  = tickUs / SUB_TICKS_PER_FRAME;
    int     subTickIdx = 0;
    int64_t nextSubTickUs = timer::WasapiClock::GetTimeUs();

    while (_running.load()) {
        // ── 精密スリープ ──
        SleepUntil(nextSubTickUs);
        int64_t now = timer::WasapiClock::GetTimeUs();

        // ── 全モード共通: 受信パケット処理 ──
        DrainAndProcessPackets();

        switch (_mode) {
        // ================================================================
        // Mode::WaitReady — 準備完了待機
        // ================================================================
        case SyncMode::WaitReady: {
            // 毎サブティックで READY 送信
            SendReady();

            // 双方 READY → WaitStart へ遷移
            if (_peerReady) {
                _mode = SyncMode::WaitStart;
                cccaster::domain::session::DebugLog("[SyncCoordinator] Mode -> WaitStart (peer READY received)");
            }
            break;
        }

        // ================================================================
        // Mode::WaitStart — 開始時刻待機（θ推定 + START 合意）
        // ================================================================
        case SyncMode::WaitStart: {
            // 毎サブティックで READY + PING 送信
            SendReady();
            SendPing();

            // θ安定 → START 送信
            if (!_startSent && _clock.IsThetaStable()) {
                int64_t startTime = now + START_MARGIN_US;
                _clock.SetLocalStartTime(startTime);
                SendStart(startTime);
                _startSent = true;
            }

            // 合意スタート時刻が決定 + 到達 → Counting へ
            int64_t agreedStart = _clock.GetAgreedStartTime();
            if (agreedStart > 0 && now >= agreedStart) {
                _mode = SyncMode::Counting;
                _state.isSynced.store(true, std::memory_order_release);
                subTickIdx = 0;  // フレームカウント開始位置をリセット
                cccaster::domain::session::DebugLog(
                    "[SyncCoordinator] Mode -> Counting. startTime=%lld us θ=%lld us drift=%.6f",
                    agreedStart, _clock.GetThetaUs(), _clock.GetDriftRate());
            }
            break;
        }

        // ================================================================
        // Mode::Counting — フレームカウント中
        // ================================================================
        case SyncMode::Counting: {
            // ── フレーム進行: 3サブティックに1回 ──
            if (subTickIdx == 0) {
                uint32_t frame = _state.currentFrame.load(std::memory_order_relaxed) + 1;
                _state.currentFrame.store(frame, std::memory_order_release);

                // 疎通カウンタ
                _framesSinceLastRecv++;
                if (_framesSinceLastRecv >= DISCONNECT_TIMEOUT_FRAMES) {
                    _state.isPeerAlive.store(false, std::memory_order_release);
                }

                // ティック周期をドリフト補正込みで算出 → サブティック更新
                tickUs = _clock.GetTickUs();
                _state.currentTickUs.store(tickUs, std::memory_order_release);
                subTickUs = tickUs / SUB_TICKS_PER_FRAME;

                // ── ローカル入力送信（≤3回/F）──
                int sends = 0;
                {
                    std::lock_guard<std::mutex> lock(_localMutex);
                    std::swap(_localQueue, _localQueueSwap);
                }
                for (const auto& entry : _localQueueSwap) {
                    (void)entry;
                    if (++sends >= MAX_SENDS_PER_FRAME) break;
                }
                _localQueueSwap.clear();

                // ── PING ハートビート (入力送信がなければ) ──
                if (sends == 0) {
                    SendPing();
                }

                // フレーム進捗ログ（60Fごと）
                if (frame % 60 == 0) {
                    cccaster::domain::session::DebugLog(
                        "[SyncCoordinator] F=%u tick=%lldus θ=%lldus drift=%.6f alive=%d",
                        frame, tickUs, _clock.GetThetaUs(),
                        _clock.GetDriftRate(),
                        _state.isPeerAlive.load() ? 1 : 0);
                }
            }

            // サブティックインデックスを巡回
            subTickIdx = (subTickIdx + 1) % SUB_TICKS_PER_FRAME;
            break;
        }
        } // switch

        // ── 次のサブティック時刻を累積 ──
        nextSubTickUs += subTickUs;
    }

    uint32_t finalFrame = _state.currentFrame.load();
    cccaster::domain::session::DebugLog("[SyncCoordinator] Thread exiting. totalFrames=%u", finalFrame);
}

} // namespace netplay
} // namespace core
} // namespace cccaster

