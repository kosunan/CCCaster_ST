// ============================================================================
// SyncCalculator.cpp — 同期計算器（実装）
// ============================================================================

#include "core_dll/mbaa_sync/protocol/SyncCalculator.hpp"
#include "core_dll/fg_netplay/sync/SyncCoordinator.hpp"
#include "core_dll/fg_netplay/sync/Metronome.hpp"
#include "core_dll/fg_netplay/common/WasapiClock.hpp"
#include "core_dll/fg_netplay/buffer/CentralBuffer.hpp"
#include "core_dll/mbaa_game/monitor/GamePhaseDetector.hpp"
#include "core_dll/mbaa_game/constants/MbaaConstants.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/ui/State_Ui_Logic.hpp"
#include <cstring>

namespace cccaster {
namespace core {
namespace netplay {

// ── パケットペイロード（SyncCoordinator.cpp と同一定義）──
#pragma pack(push, 1)
struct StartPayload {
    int64_t startTimeUs;
};
struct GameTickPayload {
    uint32_t baseFrame;
    int64_t  t_send;
    int64_t  echo_t1;
    int64_t  echo_t2;
    uint16_t buttons;
    uint16_t direction;
    uint8_t  delay;
    uint8_t  maxRollback;
    uint8_t  flags;        // bit0: introComplete（introState==0 到達を通知）
};
struct PingPayload {
    int64_t t_send;
    int64_t echo_t1;
    int64_t echo_t2;
};
#pragma pack(pop)

// ============================================================================
// BuildUnifiedPacket — CC10統一ヘッダ + ペイロードを組み立てる
// ============================================================================
std::vector<uint8_t> SyncCalculator::BuildUnifiedPacket(
    uint8_t phase, uint8_t type, int64_t timestampUs,
    const void* payload, size_t payloadSize)
{
    std::vector<uint8_t> pkt(UNIFIED_HEADER_SIZE + payloadSize, 0);
    uint32_t magic = CC10_MAGIC;
    std::memcpy(pkt.data(), &magic, sizeof(magic));
    pkt[4] = phase;
    pkt[5] = type;
    std::memcpy(pkt.data() + HDR_TIMESTAMP_OFFSET, &timestampUs, sizeof(timestampUs));
    if (payload && payloadSize > 0) {
        std::memcpy(pkt.data() + UNIFIED_HEADER_SIZE, payload, payloadSize);
    }
    return pkt;
}

// ============================================================================
// Initialize / Reset
// ============================================================================
void SyncCalculator::Initialize(bool isHost, int delayFrames, int maxRollback,
                                 Metronome* metronome) {
    _isHost = isHost;
    _delayFrames = delayFrames;
    _maxRollback = maxRollback;
    _metronome = metronome;
    Reset();
}

void SyncCalculator::Reset() {
    _clock.Reset();
    _peerReady = false;
    _framesSinceLastRecv = 0;
    _latestPeerFrame = 0;
    _lastPeerT1 = 0;
    _lastPeerRecvUs = 0;
    _delayDirty = false;
    _rollbackDirty = false;
}

// ============================================================================
// ProcessReceivedPacket — 受信パケット解析 + Θ計算 + CentralBuffer書込み
// ============================================================================
void SyncCalculator::ProcessReceivedPacket(const std::vector<uint8_t>& data,
                                            const std::string& /*fromIp*/, uint16_t /*fromPort*/,
                                            int64_t receiveTimeUs) {
    // 疎通更新
    _framesSinceLastRecv = 0;

    // 統一ヘッダ検証
    if (static_cast<int>(data.size()) < UNIFIED_HEADER_SIZE) return;
    uint32_t magic = 0;
    std::memcpy(&magic, data.data(), sizeof(magic));
    if (magic != CC10_MAGIC) return;

    uint8_t pktType = data[5];

    // ── READY パケット ──
    if (pktType == PKT_READY) {
        if (!_peerReady) {
            _peerReady = true;
            cccaster::domain::session::DebugLog("[SyncCalculator] Received READY from peer.");
        }
        return;
    }

    // ── START パケット ──
    if (pktType == PKT_START && data.size() >= UNIFIED_HEADER_SIZE + sizeof(StartPayload)) {
        StartPayload sp{};
        std::memcpy(&sp, data.data() + UNIFIED_HEADER_SIZE, sizeof(sp));
        _clock.SetPeerStartTime(sp.startTimeUs);
        cccaster::domain::session::DebugLog(
            "[SyncCalculator] Received START. peerStartTime=%lld us", sp.startTimeUs);
        return;
    }

    // ── GAME_TICK パケット ──
    if (pktType == PKT_GAME_TICK && data.size() >= UNIFIED_HEADER_SIZE + sizeof(GameTickPayload)) {
        GameTickPayload gtp{};
        std::memcpy(&gtp, data.data() + UNIFIED_HEADER_SIZE, sizeof(gtp));

        // (1) NTP θ推定
        if (gtp.echo_t1 > 0 && gtp.echo_t2 > 0) {
            _clock.AddNtpSample(gtp.echo_t1, gtp.echo_t2, gtp.t_send, receiveTimeUs);
        }

        // (2) エコー追跡更新
        _lastPeerT1 = gtp.t_send;
        _lastPeerRecvUs = receiveTimeUs;

        // (3) CentralBuffer に相手入力を確定書込み
        uint32_t remoteInput = static_cast<uint32_t>(gtp.buttons)
                             | (static_cast<uint32_t>(gtp.direction) << 16);
        cccaster::core::sync::CentralBuffer::GetInstance().ConfirmRemote(
            gtp.baseFrame, remoteInput);

        // (4) D/R 受信
        static constexpr uint8_t DR_NO_CHANGE = 0xFF;
        bool drChanged = false;
        if (gtp.delay != DR_NO_CHANGE) { _delayFrames = gtp.delay; drChanged = true; }
        if (gtp.maxRollback != DR_NO_CHANGE) { _maxRollback = gtp.maxRollback; drChanged = true; }
        if (drChanged) {
            cccaster::core::sync::CentralBuffer::GetInstance().SetSyncParams(
                _delayFrames, _maxRollback);
            cccaster::domain::ui::StateUiLogic::SetDelay(_delayFrames);
            cccaster::domain::ui::StateUiLogic::SetRollback(_maxRollback);
        }

        // (5) 相手フレーム追跡
        if (gtp.baseFrame > _latestPeerFrame) {
            _latestPeerFrame = gtp.baseFrame;
        }

        // (6) introComplete フラグ受信
        if (gtp.flags & 0x01) {
            cccaster::core::netplay::SyncCoordinator::GetMutableState()
                .peerIntroComplete.store(true, std::memory_order_release);
        }
        return;
    }

    // ── PING パケット ──
    if (pktType == 0x00 && data.size() >= UNIFIED_HEADER_SIZE + sizeof(PingPayload)) {
        PingPayload pp{};
        std::memcpy(&pp, data.data() + UNIFIED_HEADER_SIZE, sizeof(pp));
        if (pp.echo_t1 > 0 && pp.echo_t2 > 0) {
            _clock.AddNtpSample(pp.echo_t1, pp.echo_t2, pp.t_send, receiveTimeUs);
        }
        _lastPeerT1 = pp.t_send;
        _lastPeerRecvUs = receiveTimeUs;
        return;
    }
}

// ============================================================================
// 送信パケット組立て
// ============================================================================
std::vector<uint8_t> SyncCalculator::BuildGameTickPacket(uint32_t frame, uint32_t localInput) {
    int64_t now = timer::WasapiClock::GetTimeUs();
    GameTickPayload gtp{};
    gtp.baseFrame   = frame;
    gtp.t_send      = now;
    gtp.echo_t1     = _lastPeerT1;
    gtp.echo_t2     = _lastPeerRecvUs;
    gtp.buttons     = static_cast<uint16_t>(localInput & 0xFFFF);
    gtp.direction   = static_cast<uint16_t>((localInput >> 16) & 0xFFFF);

    static constexpr uint8_t DR_NO_CHANGE = 0xFF;
    gtp.delay       = _delayDirty    ? static_cast<uint8_t>(_delayFrames) : DR_NO_CHANGE;
    gtp.maxRollback = _rollbackDirty ? static_cast<uint8_t>(_maxRollback) : DR_NO_CHANGE;
    _delayDirty    = false;
    _rollbackDirty = false;

    // introComplete フラグをパケットに乗せる
    gtp.flags = cccaster::core::netplay::SyncCoordinator::GetState()
                    .localIntroComplete.load(std::memory_order_acquire) ? 0x01 : 0x00;

    return BuildUnifiedPacket(0x00, PKT_GAME_TICK, now, &gtp, sizeof(gtp));
}

std::vector<uint8_t> SyncCalculator::BuildReadyPacket() {
    int64_t now = timer::WasapiClock::GetTimeUs();
    return BuildUnifiedPacket(0x00, PKT_READY, now);
}

std::vector<uint8_t> SyncCalculator::BuildStartPacket(int64_t startTimeUs) {
    int64_t now = timer::WasapiClock::GetTimeUs();
    StartPayload sp{};
    sp.startTimeUs = startTimeUs;
    return BuildUnifiedPacket(0x00, PKT_START, now, &sp, sizeof(sp));
}

std::vector<uint8_t> SyncCalculator::BuildPingPacket() {
    int64_t now = timer::WasapiClock::GetTimeUs();
    PingPayload pp{};
    pp.t_send  = now;
    pp.echo_t1 = _lastPeerT1;
    pp.echo_t2 = _lastPeerRecvUs;
    return BuildUnifiedPacket(0x00, 0x00, now, &pp, sizeof(pp));
}

// ============================================================================
// AdvanceFrame — フレームを1つ進めて CentralBuffer に書込み
// ============================================================================
uint32_t SyncCalculator::AdvanceFrame(uint32_t localInput) {
    _currentFrame++;
    WriteFrameSlot(_currentFrame, localInput);
    return _currentFrame;
}

// ============================================================================
// WriteFrameSlot — CentralBuffer にフレームスロットを書込み
// ============================================================================
void SyncCalculator::WriteFrameSlot(uint32_t frame, uint32_t localInput) {
    auto& buf = cccaster::core::sync::CentralBuffer::GetInstance();
    uint8_t phase = static_cast<uint8_t>(
        cccaster::game_interface::GameMonitor::GetCurrentPhase());
    bool rb = (phase == static_cast<uint8_t>(
        cccaster::game_interface::GamePhase::InGame))
        && (*CC_INTRO_STATE_ADDR == 0);
    buf.CommitFrame(frame, phase, rb, localInput, 0, false);
}

// ============================================================================
// UpdateAlphaCorrections — α1/α2 を計算して Metronome に反映
// ============================================================================
void SyncCalculator::UpdateAlphaCorrections() {
    if (!_metronome) return;

    // ── α1: パケットディレイ不足補正 ──
    // RTT/2 (片道遅延) が D+R フレーム分で吸収可能か判定
    int64_t halfRtt = _clock.GetRttUs() / 2;
    int64_t absorbableUs = static_cast<int64_t>(_delayFrames + _maxRollback)
                         * Metronome::BASE_TICK_US;
    int64_t alpha1 = 0;
    if (halfRtt > absorbableUs) {
        alpha1 = halfRtt - absorbableUs;
    }
    _metronome->SetAlpha1(alpha1);

    // ── α2: 相手メトロノームとのズレ補正 ──
    // NetplayClock::GetTickUs() の α補正ロジックを流用
    int64_t alpha2 = _clock.GetTickUs() - Metronome::BASE_TICK_US;
    _metronome->SetAlpha2(alpha2);
}

} // namespace netplay
} // namespace core
} // namespace cccaster
