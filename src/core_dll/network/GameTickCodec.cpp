// ============================================================================
// GameTickCodec.cpp — 同期計算器（実装）
//
// 【パケット設計】
//   全フェーズで GAME_TICK (0x30) のみ使用。
//   flags.bit0=ready, startTimeUs>0 で WaitReady/WaitStart を表現。
// ============================================================================

#include "core_dll/network/GameTickCodec.hpp"
#include "core_dll/sync/NetplaySession.hpp"
#include "core_dll/timing/Metronome.hpp"
#include "core_dll/timing/WasapiClock.hpp"
#include "core_dll/sync/FrameInputBuffer.hpp"
#include "core_dll/detect/GamePhaseDetector.hpp"
#include "core_dll/detect/MbaaAddresses.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/ui/State_Ui_Logic.hpp"
#include <cstring>

namespace cccaster {
namespace core {
namespace netplay {

// ── 統一 GameTickPayload ──
#pragma pack(push, 1)
struct GameTickPayload {
    // NTP (常時)
    int64_t  t_send;
    int64_t  echo_t1;
    int64_t  echo_t2;
    // フレーム同期 (Counting 時のみ有効)
    uint32_t baseFrame;
    uint16_t buttons;
    uint16_t direction;
    // 同期パラメータ
    uint8_t  delay;
    uint8_t  maxRollback;
    // フラグ (READY/INTRO 統合)
    uint8_t  flags;        // bit0: ready, bit1: introComplete
    // スタート時刻 (WaitStart 時のみ有効, 0=未設定)
    int64_t  startTimeUs;
};
#pragma pack(pop)

// ============================================================================
// BuildUnifiedPacket — CC10統一ヘッダ + ペイロードを組み立てる
// ============================================================================
std::vector<uint8_t> GameTickCodec::BuildUnifiedPacket(
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
void GameTickCodec::Initialize(bool isHost, int delayFrames, int maxRollback,
                                 Metronome* metronome) {
    _isHost = isHost;
    _delayFrames = delayFrames;
    _maxRollback = maxRollback;
    _metronome = metronome;
    Reset();
}

void GameTickCodec::Reset() {
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
// ProcessReceivedPacket — 受信 GAME_TICK 解析
// ============================================================================
void GameTickCodec::ProcessReceivedPacket(const std::vector<uint8_t>& data,
                                            const std::string& /*fromIp*/, uint16_t /*fromPort*/,
                                            int64_t receiveTimeUs) {
    _framesSinceLastRecv = 0;

    // 統一ヘッダ検証
    if (static_cast<int>(data.size()) < UNIFIED_HEADER_SIZE) return;
    uint32_t magic = 0;
    std::memcpy(&magic, data.data(), sizeof(magic));
    if (magic != CC10_MAGIC) return;

    uint8_t pktType = data[5];
    if (pktType != PKT_GAME_TICK) return;
    if (data.size() < UNIFIED_HEADER_SIZE + sizeof(GameTickPayload)) return;

    GameTickPayload gtp{};
    std::memcpy(&gtp, data.data() + UNIFIED_HEADER_SIZE, sizeof(gtp));

    // (1) NTP θ推定（常時）
    if (gtp.echo_t1 > 0 && gtp.echo_t2 > 0) {
        _clock.AddNtpSample(gtp.echo_t1, gtp.echo_t2, gtp.t_send, receiveTimeUs);
    }

    // (2) エコー追跡更新
    _lastPeerT1 = gtp.t_send;
    _lastPeerRecvUs = receiveTimeUs;

    // (3) READY フラグ
    if (gtp.flags & FLAG_READY) {
        if (!_peerReady) {
            _peerReady = true;
            cccaster::domain::session::DebugLog("[GameTickCodec] Peer READY received.");
        }
    }

    // (4) START 時刻
    if (gtp.startTimeUs > 0) {
        _clock.SetPeerStartTime(gtp.startTimeUs);
        cccaster::domain::session::DebugLog(
            "[GameTickCodec] Peer startTime=%lld us", gtp.startTimeUs);
    }

    // (5) FrameInputBuffer に相手入力を確定書込み（フレーム>0 なら Counting 中）
    if (gtp.baseFrame > 0) {
        uint32_t remoteInput = static_cast<uint32_t>(gtp.buttons)
                             | (static_cast<uint32_t>(gtp.direction) << 16);
        cccaster::core::sync::FrameInputBuffer::GetInstance().ConfirmRemote(
            gtp.baseFrame, remoteInput);
    }

    // (6) D/R 受信
    static constexpr uint8_t DR_NO_CHANGE = 0xFF;
    bool drChanged = false;
    if (gtp.delay != DR_NO_CHANGE) { _delayFrames = gtp.delay; drChanged = true; }
    if (gtp.maxRollback != DR_NO_CHANGE) { _maxRollback = gtp.maxRollback; drChanged = true; }
    if (drChanged) {
        cccaster::core::sync::FrameInputBuffer::GetInstance().SetSyncParams(
            _delayFrames, _maxRollback);
        cccaster::domain::ui::StateUiLogic::SetDelay(_delayFrames);
        cccaster::domain::ui::StateUiLogic::SetRollback(_maxRollback);
    }

    // (7) 相手フレーム追跡
    if (gtp.baseFrame > _latestPeerFrame) {
        _latestPeerFrame = gtp.baseFrame;
    }

    // (8) introComplete フラグ受信
    if (gtp.flags & FLAG_INTRO_COMPLETE) {
        cccaster::core::netplay::NetplaySession::GetMutableState()
            .peerIntroComplete.store(true, std::memory_order_release);
    }
}

// ============================================================================
// BuildGameTickPacket — 全フェーズ共通パケット組立て
// ============================================================================
std::vector<uint8_t> GameTickCodec::BuildGameTickPacket(
    uint32_t frame, uint32_t localInput, bool ready, int64_t startTimeUs)
{
    int64_t now = timer::WasapiClock::GetTimeUs();
    GameTickPayload gtp{};
    gtp.t_send      = now;
    gtp.echo_t1     = _lastPeerT1;
    gtp.echo_t2     = _lastPeerRecvUs;
    gtp.baseFrame   = frame;
    gtp.buttons     = static_cast<uint16_t>(localInput & 0xFFFF);
    gtp.direction   = static_cast<uint16_t>((localInput >> 16) & 0xFFFF);

    static constexpr uint8_t DR_NO_CHANGE = 0xFF;
    gtp.delay       = _delayDirty    ? static_cast<uint8_t>(_delayFrames) : DR_NO_CHANGE;
    gtp.maxRollback = _rollbackDirty ? static_cast<uint8_t>(_maxRollback) : DR_NO_CHANGE;
    _delayDirty    = false;
    _rollbackDirty = false;

    // flags
    gtp.flags = 0;
    if (ready) gtp.flags |= FLAG_READY;
    if (cccaster::core::netplay::NetplaySession::GetState()
            .localIntroComplete.load(std::memory_order_acquire)) {
        gtp.flags |= FLAG_INTRO_COMPLETE;
    }

    gtp.startTimeUs = startTimeUs;

    return BuildUnifiedPacket(0x00, PKT_GAME_TICK, now, &gtp, sizeof(gtp));
}

// ============================================================================
// AdvanceFrame — フレームを1つ進めて FrameInputBuffer に書込み
// ============================================================================
uint32_t GameTickCodec::AdvanceFrame(uint32_t localInput) {
    _currentFrame++;
    WriteFrameSlot(_currentFrame, localInput);
    return _currentFrame;
}

// ============================================================================
// WriteFrameSlot — FrameInputBuffer にフレームスロットを書込み
// ============================================================================
void GameTickCodec::WriteFrameSlot(uint32_t frame, uint32_t localInput) {
    auto& buf = cccaster::core::sync::FrameInputBuffer::GetInstance();
    uint8_t phase = static_cast<uint8_t>(
        cccaster::game_interface::PhaseMonitor::GetCurrentPhase());
    bool rb = (phase == static_cast<uint8_t>(
        cccaster::game_interface::GamePhase::InGame))
        && (*CC_INTRO_STATE_ADDR == 0);
    buf.WriteSlot(frame, phase, rb, localInput, 0, false);
    buf.SetWriteHead(frame);
}

// ============================================================================
// UpdateAlphaCorrections — α1/α2 を計算して Metronome に反映
// ============================================================================
void GameTickCodec::UpdateAlphaCorrections() {
    if (!_metronome) return;

    int64_t halfRtt = _clock.GetRttUs() / 2;
    int64_t absorbableUs = static_cast<int64_t>(_delayFrames + _maxRollback)
                         * Metronome::BASE_TICK_US;
    int64_t alpha1 = 0;
    if (halfRtt > absorbableUs) {
        alpha1 = halfRtt - absorbableUs;
    }
    _metronome->SetAlpha1(alpha1);

    int64_t alpha2 = _clock.GetTickUs() - Metronome::BASE_TICK_US;
    _metronome->SetAlpha2(alpha2);
}

} // namespace netplay
} // namespace core
} // namespace cccaster
