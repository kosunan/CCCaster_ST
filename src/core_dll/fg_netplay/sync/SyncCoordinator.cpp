// ============================================================================
// SyncCoordinator.cpp — 通信スレッド統括（4層分離版）
//
// 【設計】
//   通信スレッドはパケット送受信に専念する。
//   パケット解析・Θ計算・α補正・CentralBuffer操作は SyncCalculator に委譲。
//   フレームリズム生成は Metronome に委譲。
// ============================================================================

#include "core_dll/fg_netplay/sync/SyncCoordinator.hpp"
#include "core_dll/fg_netplay/common/WasapiClock.hpp"
#include "core_dll/fg_netplay/network/NetplayManager.hpp"
#include "core_dll/fg_netplay/buffer/CentralBuffer.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/ui/State_Ui_Logic.hpp"
#include <windows.h>

namespace cccaster {
namespace core {
namespace netplay {

// ─── シングルトン ──────────────────────────────────────
SyncCoordinator& SyncCoordinator::GetInstance() {
    static SyncCoordinator instance;
    return instance;
}

// ============================================================================
// Start — 通信スレッド + メトロノームを起動
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
    _startSent    = false;
    _lastSentFrame = 0;
    _lastLogFrame  = 0;
    _peerActualPort = 0;

    // SharedSyncState リセット
    _state.currentTickUs.store(Metronome::BASE_TICK_US);
    _state.isSynced.store(false);
    _state.isPeerAlive.store(false);
    _state.peerReady.store(false);
    _state.clockOffsetUs.store(0);
    _state.lastRttUs.store(0);

    // CentralBuffer 初期化
    cccaster::core::sync::CentralBuffer::GetInstance().Initialize(200, delayFrames, maxRollback, isHost);

    // オーバーレイ初期表示
    cccaster::domain::ui::StateUiLogic::SetDelay(delayFrames);
    cccaster::domain::ui::StateUiLogic::SetRollback(maxRollback);

    // SyncCalculator 初期化
    _calc.Initialize(isHost, delayFrames, maxRollback, &_metronome);

    // キュークリア
    {
        std::lock_guard<std::mutex> lock(_recvMutex);
        _recvQueue.clear();
        _recvQueueSwap.clear();
    }

    // モード初期化
    _mode = SyncMode::WaitReady;

    cccaster::domain::session::DebugLog(
        "[SyncCoordinator] Starting. host=%d target=%s:%u localPort=%u delay=%d maxRB=%d",
        isHost, targetIp.c_str(), targetPort, localPort, delayFrames, maxRollback);

    _running.store(true);
    _thread = std::thread(&SyncCoordinator::ThreadMain, this);
}

// ============================================================================
// Stop — 通信スレッド + メトロノームを停止
// ============================================================================
void SyncCoordinator::Stop() {
    if (!_running.load()) return;
    _running.store(false);
    _metronome.Stop();
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
// DrainAndProcessPackets — 受信キューを drain して SyncCalculator に委譲
// ============================================================================
void SyncCoordinator::DrainAndProcessPackets() {
    {
        std::lock_guard<std::mutex> lock(_recvMutex);
        std::swap(_recvQueue, _recvQueueSwap);
    }

    for (const auto& pkt : _recvQueueSwap) {
        // 疎通更新
        _state.isPeerAlive.store(true, std::memory_order_release);
        _peerActualPort = pkt.fromPort;

        // SyncCalculator に処理を委譲
        _calc.ProcessReceivedPacket(pkt.data, pkt.fromIp, pkt.fromPort, pkt.receiveTimeUs);

        // SharedSyncState 更新
        _state.clockOffsetUs.store(_calc.GetThetaUs(), std::memory_order_release);
        _state.lastRttUs.store(_calc.GetRttUs(), std::memory_order_release);
        if (_calc.IsPeerReady()) {
            _state.peerReady.store(true, std::memory_order_release);
        }
    }

    _recvQueueSwap.clear();
}

// ============================================================================
// SendPacket — NetplayManager 経由で送信
// ============================================================================
void SyncCoordinator::SendPacket(const std::vector<uint8_t>& data) {
    auto sendFunc = cccaster::netplay::NetplayManager::GetInstance().GetSendFunc();
    if (sendFunc) {
        sendFunc(data);
    }
}

// ============================================================================
// SleepUntil — 精密スリープ
// ============================================================================
void SyncCoordinator::SleepUntil(int64_t targetUs) {
    while (true) {
        int64_t remain = targetUs - timer::WasapiClock::GetTimeUs();
        if (remain <= 0) break;
        if (remain > 2000) {
            Sleep(1);
        } else {
            YieldProcessor();
        }
    }
}

// ============================================================================
// ThreadMain — 通信スレッドのメインループ（4層分離版）
// ============================================================================
//
// 【設計】
//   通信スレッドはパケット送受信に専念。
//   固定間隔（BASE_TICK_US / SUB_TICKS_PER_FRAME）でループ。
//   α補正はメトロノームが独立管理し、通信間隔に影響しない。
//
void SyncCoordinator::ThreadMain() {
    cccaster::domain::session::DebugLog("[SyncCoordinator] Thread started. Mode=WaitReady");

    // 通信スレッドは固定間隔でループ（α補正の影響を受けない）
    int64_t subTickUs  = Metronome::BASE_TICK_US / SUB_TICKS_PER_FRAME;
    int     subTickIdx = 0;
    int64_t nextSubTickUs = timer::WasapiClock::GetTimeUs();

    while (_running.load()) {
        // ── 精密スリープ ──
        SleepUntil(nextSubTickUs);
        int64_t now = timer::WasapiClock::GetTimeUs();

        // ── 受信パケット処理 ──
        DrainAndProcessPackets();

        switch (_mode) {
        // ================================================================
        // Mode::WaitReady — 準備完了待機
        // ================================================================
        case SyncMode::WaitReady: {
            SendPacket(_calc.BuildGameTickPacket(0, 0, true, 0));

            if (_calc.IsPeerReady()) {
                _mode = SyncMode::WaitStart;
                cccaster::domain::session::DebugLog(
                    "[SyncCoordinator] Mode -> WaitStart (peer READY received)");
            }
            break;
        }

        // ================================================================
        // Mode::WaitStart — 開始時刻待機
        // ================================================================
        case SyncMode::WaitStart: {
            // θ安定前: ready のみ送信（NTP echo は常に含まれる）
            // θ安定後: ready + startTimeUs を送信
            int64_t sendStartTime = 0;
            if (!_startSent && _calc.IsThetaStable()) {
                sendStartTime = now + START_MARGIN_US;
                _calc.SetLocalStartTime(sendStartTime);
                _startSent = true;
            } else if (_startSent) {
                sendStartTime = _calc.GetAgreedStartTime() > 0
                    ? _calc.GetAgreedStartTime() : 0;
            }
            SendPacket(_calc.BuildGameTickPacket(0, 0, true, sendStartTime));

            int64_t agreedStart = _calc.GetAgreedStartTime();
            if (agreedStart > 0 && now >= agreedStart) {
                _mode = SyncMode::Counting;
                _calc.SetBaselineTheta();
                _state.isSynced.store(true, std::memory_order_release);

                // フレーム番号初期化 + メトロノーム起動
                uint32_t startFrame = cccaster::core::sync::CentralBuffer::GetInstance().GetWriteHead();
                _calc.SetInitialFrame(startFrame);
                _metronome.Start();

                subTickIdx = 0;
                cccaster::domain::session::DebugLog(
                    "[SyncCoordinator] Mode -> Counting. startTime=%lld us θ=%lld us startFrame=%u",
                    agreedStart, _calc.GetThetaUs(), startFrame);
            }
            break;
        }

        // ================================================================
        // Mode::Counting — CB writeHead 監視 → パケット送信（送信専念）
        // ================================================================
        case SyncMode::Counting: {
            // 疎通カウンタ + α補正
            _calc.IncrementFrameCount();
            _state.isPeerAlive.store(_calc.IsPeerAlive(), std::memory_order_release);
            _state.currentTickUs.store(_metronome.GetCurrentIntervalUs(), std::memory_order_release);
            _calc.UpdateAlphaCorrections();

            // CB writeHead 監視 → 新フレームがあれば送信
            uint32_t newHead = cccaster::core::sync::CentralBuffer::GetInstance().GetWriteHead();
            if (newHead > _lastSentFrame) {
                const auto& slot = cccaster::core::sync::CentralBuffer::GetInstance().GetSlot(newHead);
                SendPacket(_calc.BuildGameTickPacket(newHead, slot.localInput));
                _lastSentFrame = newHead;
            }

            // 60Fごとの進捗ログ
            if (newHead % 60 == 0 && newHead != _lastLogFrame) {
                cccaster::domain::session::DebugLog(
                    "[SyncCoordinator] wh=%u tick=%lldus α1=%lld α2=%lld RTT=%lldus peerF=%u",
                    newHead, _metronome.GetCurrentIntervalUs(),
                    _metronome.GetAlpha1(), _metronome.GetAlpha2(),
                    _calc.GetRttUs(), _calc.GetLatestPeerFrame());
                _lastLogFrame = newHead;
            }

            subTickIdx = (subTickIdx + 1) % SUB_TICKS_PER_FRAME;
            break;
        }
        } // switch

        nextSubTickUs += subTickUs;
    }

    cccaster::domain::session::DebugLog("[SyncCoordinator] Thread exiting.");
}

} // namespace netplay
} // namespace core
} // namespace cccaster
