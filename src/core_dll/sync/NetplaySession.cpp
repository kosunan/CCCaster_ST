// ============================================================================
// NetplaySession.cpp — 通信スレッド統括
//
// 【設計】
//   通信スレッドはパケット送受信に専念する。
//   パケット解析・Θ計算・α補正・FrameInputBuffer操作は SyncCodec に委譲。
//   フレームリズム生成は Metronome に委譲。
// ============================================================================

#include "core_dll/sync/NetplaySession.hpp"
#include "core_dll/timing/WasapiClock.hpp"
#include "core_dll/network/NetplayManager.hpp"
#include "core_dll/sync/MenuInputBuffer.hpp"
#include "core_dll/sync/MatchInputBuffer.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/ui/State_Ui_Logic.hpp"
#include <windows.h>

namespace cccaster {
namespace core {
namespace netplay {

// ─── シングルトン ──────────────────────────────────────
NetplaySession& NetplaySession::GetInstance() {
    static NetplaySession instance;
    return instance;
}

// ============================================================================
// Start — 通信スレッド + メトロノームを起動
// ============================================================================
void NetplaySession::Start(bool isHost,
                            const std::string& targetIp, uint16_t targetPort,
                            uint16_t localPort,
                            int delayFrames, int maxRollback) {
    if (_running.load()) return;

    _isHost       = isHost;
    _targetIp     = targetIp;
    _targetPort   = targetPort;
    _localPort    = localPort;
    _startSent    = false;
    _lastSentMenuFrame  = 0;
    _lastSentMatchFrame = 0;
    _lastLogFrame       = 0;
    _peerActualPort = 0;

    // SharedSyncState リセット
    _state.currentTickUs.store(Metronome::BASE_TICK_US);
    _state.isSynced.store(false);
    _state.isPeerAlive.store(false);
    _state.peerReady.store(false);
    _state.clockOffsetUs.store(0);
    _state.lastRttUs.store(0);

    // InputBuffer 初期化
    cccaster::core::sync::MenuInputBuffer::GetInstance().Initialize(200, delayFrames);
    cccaster::core::sync::MatchInputBuffer::GetInstance().Initialize(200, delayFrames, maxRollback);

    // オーバーレイ初期表示
    cccaster::domain::ui::StateUiLogic::SetDelay(delayFrames);
    cccaster::domain::ui::StateUiLogic::SetRollback(maxRollback);

    // SyncCodec 初期化
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
        "[NetplaySession] Starting. host=%d target=%s:%u localPort=%u delay=%d maxRB=%d",
        isHost, targetIp.c_str(), targetPort, localPort, delayFrames, maxRollback);

    _running.store(true);
    _thread = std::thread(&NetplaySession::ThreadMain, this);
}

// ============================================================================
// Stop — 通信スレッド + メトロノームを停止
// ============================================================================
void NetplaySession::Stop() {
    if (!_running.load()) return;
    _running.store(false);
    _metronome.Stop();
    if (_thread.joinable()) {
        _thread.join();
    }
    cccaster::domain::session::DebugLog("[NetplaySession] Stopped.");
}

// ============================================================================
// OnPacketReceived — 受信スレッドから呼ばれ、キューに Push するだけ
// ============================================================================
void NetplaySession::OnPacketReceived(const std::vector<uint8_t>& data,
                                        const std::string& fromIp, uint16_t fromPort) {
    int64_t receiveTime = timer::WasapiClock::GetTimeUs();
    std::lock_guard<std::mutex> lock(_recvMutex);
    _recvQueue.push_back({data, fromIp, fromPort, receiveTime});
}

// ============================================================================
// DrainAndProcessPackets — 受信キューを drain して SyncCodec に委譲
// ============================================================================
void NetplaySession::DrainAndProcessPackets() {
    {
        std::lock_guard<std::mutex> lock(_recvMutex);
        std::swap(_recvQueue, _recvQueueSwap);
    }

    for (const auto& pkt : _recvQueueSwap) {
        // 疎通更新
        _state.isPeerAlive.store(true, std::memory_order_release);
        _peerActualPort = pkt.fromPort;

        // SyncCodec に処理を委譲
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
void NetplaySession::SendPacket(const std::vector<uint8_t>& data) {
    auto sendFunc = cccaster::netplay::NetplayManager::GetInstance().GetSendFunc();
    if (sendFunc) {
        sendFunc(data);
    }
}

// ============================================================================
// SleepUntil — 精密スリープ
// ============================================================================
void NetplaySession::SleepUntil(int64_t targetUs) {
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
// ThreadMain — 通信スレッドのメインループ
// ============================================================================
//
// 【設計】
//   通信スレッドはパケット送受信に専念。
//   1フレーム間隔（BASE_TICK_US ≈ 16.6ms）でループ。
//   待機は SleepUntil（14ms Sleep + 残りCPUスピン）で精密制御。
//   α補正はメトロノームが独立管理し、通信間隔に影響しない。
//
//   Counting モードでは以下2つのソースを監視:
//     (1) CB writeHead 変化 → ゲームデータ送信
//     (2) needKeepalive フラグ → 定期 keepalive 送信
//
void NetplaySession::ThreadMain() {
    cccaster::domain::session::DebugLog("[NetplaySession] Thread started. Mode=WaitReady");

    int64_t nextTickUs = timer::WasapiClock::GetTimeUs();

    while (_running.load()) {
        // ── 精密スリープ（14ms Sleep + 残りCPUスピン）──
        SleepUntil(nextTickUs);
        int64_t now = timer::WasapiClock::GetTimeUs();

        // ── 受信パケット処理 ──
        DrainAndProcessPackets();

        switch (_mode) {
        // ================================================================
        // Mode::WaitReady — 準備完了待機
        // ================================================================
        case SyncMode::WaitReady: {
            SendPacket(_calc.BuildPacket(0, 0, true, 0));

            if (_calc.IsPeerReady()) {
                _mode = SyncMode::WaitStart;
                cccaster::domain::session::DebugLog(
                    "[NetplaySession] Mode -> WaitStart (peer READY received)");
            }
            break;
        }

        // ================================================================
        // Mode::WaitStart — 開始時刻待機
        // ================================================================
        case SyncMode::WaitStart: {
            int64_t sendStartTime = 0;
            if (!_startSent && _calc.IsThetaStable()) {
                sendStartTime = now + START_MARGIN_US;
                _calc.SetLocalStartTime(sendStartTime);
                _startSent = true;
            } else if (_startSent) {
                sendStartTime = _calc.GetAgreedStartTime() > 0
                    ? _calc.GetAgreedStartTime() : 0;
            }
            SendPacket(_calc.BuildPacket(0, 0, true, sendStartTime));

            int64_t agreedStart = _calc.GetAgreedStartTime();
            if (agreedStart > 0 && now >= agreedStart) {
                _mode = SyncMode::Counting;
                _calc.SetBaselineTheta();
                _state.isSynced.store(true, std::memory_order_release);

                uint32_t startFrame = cccaster::core::sync::MatchInputBuffer::GetInstance().GetWriteHead();
                _metronome.Start();

                cccaster::domain::session::DebugLog(
                    "[NetplaySession] Mode -> Counting. startTime=%lld us θ=%lld us startFrame=%u",
                    agreedStart, _calc.GetThetaUs(), startFrame);
            }
            break;
        }

        // ================================================================
        // Mode::Counting — CB writeHead + needKeepalive 監視
        // ================================================================
        case SyncMode::Counting: {
            // 疎通カウンタ + α補正
            _calc.IncrementFrameCount();
            _state.isPeerAlive.store(_calc.IsPeerAlive(), std::memory_order_release);
            _state.currentTickUs.store(_metronome.GetCurrentIntervalUs(), std::memory_order_release);
            _calc.UpdateAlphaCorrections();

            // (1) CB writeHead 監視 → ゲームデータ送信
            uint32_t newMenuHead = cccaster::core::sync::MenuInputBuffer::GetInstance().GetWriteHead();
            uint32_t newMatchHead = cccaster::core::sync::MatchInputBuffer::GetInstance().GetWriteHead();
            
            bool sent = false;
            if (newMenuHead > _lastSentMenuFrame) {
                const auto& slot = cccaster::core::sync::MenuInputBuffer::GetInstance().GetSlot(newMenuHead);
                SendPacket(_calc.BuildPacket(newMenuHead, slot.localInput, false, 0, SyncCodec::FLAG_BUFFER_MENU));
                _lastSentMenuFrame = newMenuHead;
                _keepaliveCounter = 0;
                sent = true;
            }
            if (!sent && newMatchHead > _lastSentMatchFrame) {
                const auto& slot = cccaster::core::sync::MatchInputBuffer::GetInstance().GetSlot(newMatchHead);
                SendPacket(_calc.BuildPacket(newMatchHead, slot.localInput, false, 0, SyncCodec::FLAG_BUFFER_MATCH));
                _lastSentMatchFrame = newMatchHead;
                _keepaliveCounter = 0;
                sent = true;
            }
            
            if (!sent && _state.needKeepalive.load(std::memory_order_acquire)) {
                // (2) CB書込みなし + keepalive要求 → 定期 keepalive
                _keepaliveCounter++;
                if (_keepaliveCounter >= KEEPALIVE_INTERVAL_FRAMES) {
                    if (newMatchHead > 0) {
                        SendPacket(_calc.BuildPacket(newMatchHead, 0, false, 0, SyncCodec::FLAG_BUFFER_MATCH));
                    } else if (newMenuHead > 0) {
                        SendPacket(_calc.BuildPacket(newMenuHead, 0, false, 0, SyncCodec::FLAG_BUFFER_MENU));
                    } else {
                        SendPacket(_calc.BuildPacket(0, 0, false, 0, 0));
                    }
                    _keepaliveCounter = 0;
                }
            }

            // 進捗ログ（60フレームごと）
            if (newMatchHead % 60 == 0 && newMatchHead != _lastLogFrame) {
                cccaster::domain::session::DebugLog(
                    "[NetplaySession] whMenu=%u whMatch=%u tick=%lldus α1=%lld α2=%lld RTT=%lldus peerF=%u",
                    newMenuHead, newMatchHead, _metronome.GetCurrentIntervalUs(),
                    _metronome.GetAlpha1(), _metronome.GetAlpha2(),
                    _calc.GetRttUs(), _calc.GetLatestPeerFrame());
                _lastLogFrame = newMatchHead;
            }
            break;
        }
        } // switch

        nextTickUs += Metronome::BASE_TICK_US;
    }

    cccaster::domain::session::DebugLog("[NetplaySession] Thread exiting.");
}

} // namespace netplay
} // namespace core
} // namespace cccaster
