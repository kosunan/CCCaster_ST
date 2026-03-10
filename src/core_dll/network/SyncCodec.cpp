// ============================================================================
// SyncCodec.cpp — 同期計算器（実装）
//
// 【パケット設計】
//   全フェーズで SYNC_TICK (0x30) のみ使用。
//   flags.bit0=ready, bit1=phaseReady, startTimeUs>0 で WaitReady/WaitStart を表現。

#include "core_dll/network/SyncCodec.hpp"
#include "core_dll/sync/NetplaySession.hpp"
#include "core_dll/timing/Metronome.hpp"
#include "core_dll/timing/WasapiClock.hpp"
#include "core_dll/sync/MenuInputBuffer.hpp"
#include "core_dll/sync/MatchInputBuffer.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/engine/MatchScene.hpp"
#include "core_dll/ui/State_Ui_Logic.hpp"
#include <cstring>

namespace cccaster {
namespace core {
namespace netplay {

// ── 統一 SyncPayload ──
#pragma pack(push, 1)
struct SyncPayload {
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
    // フラグ (READY/PHASE_READY 統合)
    uint8_t  flags;        // bit0: ready, bit1: phaseReady
    // スタート時刻 (WaitStart 時のみ有効, 0=未設定)
    int64_t  startTimeUs;
    // Rematch メニュー選択 (-1=未決定, 0=もう1回, 1=キャラ選択, 2=リプレイ保存)
    int8_t   retryMenuIndex;
    // Phase 遷移同期: InGame 開始時の writeHead 基準点 (0=未設定)
    uint32_t phaseBaseFrame;
};
#pragma pack(pop)

// ============================================================================
// BuildUnifiedPacket — CC10統一ヘッダ + ペイロードを組み立てる
// ============================================================================
std::vector<uint8_t> SyncCodec::BuildUnifiedPacket(
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
void SyncCodec::Initialize(bool isHost, int delayFrames, int maxRollback,
                                 Metronome* metronome) {
    _isHost = isHost;
    _delayFrames = delayFrames;
    _maxRollback = maxRollback;
    _metronome = metronome;
    Reset();
}

void SyncCodec::Reset() {
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
void SyncCodec::ProcessReceivedPacket(const std::vector<uint8_t>& data,
                                            const std::string& /*fromIp*/, uint16_t /*fromPort*/,
                                            int64_t receiveTimeUs) {
    _framesSinceLastRecv = 0;

    // 統一ヘッダ検証
    if (static_cast<int>(data.size()) < UNIFIED_HEADER_SIZE) return;
    uint32_t magic = 0;
    std::memcpy(&magic, data.data(), sizeof(magic));
    if (magic != CC10_MAGIC) return;

    uint8_t pktType = data[5];
    if (pktType != PKT_SYNC_TICK) return;
    if (data.size() < UNIFIED_HEADER_SIZE + sizeof(SyncPayload)) return;

    SyncPayload gtp{};
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
            cccaster::domain::session::DebugLog("[SyncCodec] Peer READY received.");
        }
    }

    // (4) START 時刻
    if (gtp.startTimeUs > 0) {
        _clock.SetPeerStartTime(gtp.startTimeUs);
        cccaster::domain::session::DebugLog(
            "[SyncCodec] Peer startTime=%lld us", gtp.startTimeUs);
    }

    // (5) 該当バッファへの相手入力確定書込み（フレーム>0 なら Counting 中）
    if (gtp.baseFrame > 0) {
        uint32_t remoteInput = static_cast<uint32_t>(gtp.buttons)
                             | (static_cast<uint32_t>(gtp.direction) << 16);
        if (gtp.flags & FLAG_BUFFER_MENU) {
            cccaster::core::sync::MenuInputBuffer::GetInstance().ConfirmRemote(gtp.baseFrame, remoteInput);
        } else if (gtp.flags & FLAG_BUFFER_MATCH) {
            cccaster::core::sync::MatchInputBuffer::GetInstance().ConfirmRemote(gtp.baseFrame, remoteInput);
        }
    }

    // (6) D/R 受信
    static constexpr uint8_t DR_NO_CHANGE = 0xFF;
    bool drChanged = false;
    if (gtp.delay != DR_NO_CHANGE) { _delayFrames = gtp.delay; drChanged = true; }
    if (gtp.maxRollback != DR_NO_CHANGE) { _maxRollback = gtp.maxRollback; drChanged = true; }
    if (drChanged) {
        cccaster::core::sync::MenuInputBuffer::GetInstance().SetDelay(_delayFrames);
        cccaster::core::sync::MatchInputBuffer::GetInstance().SetSyncParams(_delayFrames, _maxRollback);
        cccaster::domain::ui::StateUiLogic::SetDelay(_delayFrames);
        cccaster::domain::ui::StateUiLogic::SetRollback(_maxRollback);
    }

    // (7) 相手フレーム追跡
    if (gtp.baseFrame > _latestPeerFrame) {
        _latestPeerFrame = gtp.baseFrame;
    }

    // (8) phaseReady フラグ受信
    if (gtp.flags & FLAG_PHASE_READY) {
        cccaster::core::netplay::NetplaySession::GetMutableState()
            .peerPhaseReady.store(true, std::memory_order_release);
    }

    // (9) Rematch メニュー選択受信
    if (gtp.retryMenuIndex >= 0) {
        cccaster::domain::scene::MatchScene::SetRemoteRetryMenuIndex(gtp.retryMenuIndex);
    }

    // (10) Peer の phaseBaseFrame 受信
    if (gtp.phaseBaseFrame > 0) {
        cccaster::core::netplay::NetplaySession::GetMutableState()
            .peerPhaseBaseFrame.store(gtp.phaseBaseFrame, std::memory_order_release);
    }
}

// ============================================================================
// BuildPacket — 全フェーズ共通パケット組立て
// ============================================================================
std::vector<uint8_t> SyncCodec::BuildPacket(
    uint32_t frame, uint32_t localInput, bool ready, int64_t startTimeUs, uint8_t bufferTargetFlag)
{
    int64_t now = timer::WasapiClock::GetTimeUs();
    SyncPayload gtp{};
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
            .localPhaseReady.load(std::memory_order_acquire)) {
        gtp.flags |= FLAG_PHASE_READY;
    }
    gtp.flags |= bufferTargetFlag;

    gtp.startTimeUs = startTimeUs;

    // Rematch メニュー選択
    gtp.retryMenuIndex = cccaster::core::netplay::NetplaySession::GetState()
        .localRetryMenuIndex.load(std::memory_order_acquire);

    // Phase 遷移同期: phaseBaseFrame
    gtp.phaseBaseFrame = cccaster::core::netplay::NetplaySession::GetState()
        .phaseBaseFrame.load(std::memory_order_acquire);

    return BuildUnifiedPacket(0x00, PKT_SYNC_TICK, now, &gtp, sizeof(gtp));
}

// ============================================================================
// UpdateAlphaCorrections — α1/α2 を計算して Metronome に反映
// ============================================================================
void SyncCodec::UpdateAlphaCorrections() {
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
