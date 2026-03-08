#pragma once
// ============================================================================
// GameTickCodec — 同期計算器
//
// 【責務】
//   - 受信パケット解析 → Θ/RTT 計算（NetplayClock 利用）
//   - α1 算出: RTT/2 ベースのパケットディレイ不足補正
//   - α2 算出: Θ変化量ベースの相手ドリフト補正
//   - FrameInputBuffer 書込み（入力データ）
//   - 送信パケット組立て（GAME_TICK のみ）
//   - D/R dirty 管理
//
// 【パケット設計】
//   全フェーズ（WaitReady/WaitStart/Counting）で GAME_TICK 1種類のみ使用。
//   フェーズの違いは flags と startTimeUs フィールドで表現する。
//
// 【スレッド安全性】
//   ProcessReceivedPacket() は通信スレッドから呼ばれる。
//   α1/α2 は Metronome に atomic 経由で供給。
// ============================================================================

#include <cstdint>
#include <vector>
#include <string>
#include "core_dll/fg_netplay/frame_sync/NetplayClock.hpp"

namespace cccaster {
namespace core {
namespace netplay {

class Metronome;  // 前方宣言

class GameTickCodec {
public:
    // ─── 初期化 ─────────────────────────────────────────
    void Initialize(bool isHost, int delayFrames, int maxRollback,
                    Metronome* metronome);
    void Reset();

    // ─── 受信パケット処理 ────────────────────────────────
    // 通信スレッドから呼ばれる。パケット解析・Θ計算・FrameInputBuffer書込みを行う。
    void ProcessReceivedPacket(const std::vector<uint8_t>& data,
                               const std::string& fromIp, uint16_t fromPort,
                               int64_t receiveTimeUs);

    // ─── 送信パケット組立て ──────────────────────────────
    /// 全フェーズ共通の GAME_TICK パケットを構築する。
    /// @param frame     フレーム番号 (Counting時のみ有効、それ以外は0)
    /// @param localInput ローカル入力 (Counting時のみ有効)
    /// @param ready     true: 準備完了シグナル (WaitReady/WaitStart)
    /// @param startTimeUs メトロノーム開始時刻 (WaitStart時のみ有効、0=未設定)
    std::vector<uint8_t> BuildGameTickPacket(uint32_t frame, uint32_t localInput,
                                             bool ready = false, int64_t startTimeUs = 0);

    // ─── フレーム番号管理 ────────────────────────────
    void SetInitialFrame(uint32_t frame) { _currentFrame = frame; }
    uint32_t AdvanceFrame(uint32_t localInput);
    uint32_t GetCurrentFrame() const { return _currentFrame; }

    // ─── FrameInputBuffer書込み ────────────────────────
    void WriteFrameSlot(uint32_t frame, uint32_t localInput);

    // ─── α補正の更新 ────────────────────────────────────
    void UpdateAlphaCorrections();

    // ─── D/R 動的変更 ───────────────────────────────────
    void SetDelayFrames(int d)  { _delayFrames = d; _delayDirty = true; }
    void SetMaxRollback(int r)  { _maxRollback = r; _rollbackDirty = true; }
    int  GetDelayFrames() const { return _delayFrames; }
    int  GetMaxRollback() const { return _maxRollback; }

    // ─── 時計データ読取り（オーバーレイ用）───────────────
    int64_t GetRttUs() const { return _clock.GetRttUs(); }
    int64_t GetThetaUs() const { return _clock.GetThetaUs(); }
    int64_t GetBaselineTheta() const { return _clock.GetBaselineTheta(); }
    bool    IsThetaStable() const { return _clock.IsThetaStable(); }

    // ─── スタート時刻管理（ハンドシェイク用）────────────
    void SetLocalStartTime(int64_t us)  { _clock.SetLocalStartTime(us); }
    void SetPeerStartTime(int64_t us)   { _clock.SetPeerStartTime(us); }
    int64_t GetAgreedStartTime() const  { return _clock.GetAgreedStartTime(); }
    void SetBaselineTheta()             { _clock.SetBaselineTheta(); }

    // ─── 疎通管理 ───────────────────────────────────────
    bool IsPeerAlive() const   { return _framesSinceLastRecv < DISCONNECT_TIMEOUT_FRAMES; }
    bool IsPeerReady() const   { return _peerReady; }
    void IncrementFrameCount() { _framesSinceLastRecv++; }
    uint32_t GetLatestPeerFrame() const { return _latestPeerFrame; }

    // ─── 定数 ──────────────────────────────────────────
    static constexpr int DISCONNECT_TIMEOUT_FRAMES = 180;

    // 統一ヘッダ
    static constexpr int      HDR_TIMESTAMP_OFFSET = 8;
    static constexpr int      UNIFIED_HEADER_SIZE  = 20;
    static constexpr uint32_t CC10_MAGIC           = 0x30314343u;

    // パケットタイプ（GAME_TICK のみ）
    static constexpr uint8_t PKT_GAME_TICK = 0x30;

    // GameTickPayload flags
    static constexpr uint8_t FLAG_READY          = 0x01;  // 準備完了
    static constexpr uint8_t FLAG_INTRO_COMPLETE = 0x02;  // イントロ完了

private:
    static std::vector<uint8_t> BuildUnifiedPacket(
        uint8_t phase, uint8_t type, int64_t timestampUs,
        const void* payload = nullptr, size_t payloadSize = 0);

    // ─── 計算エンジン ──────────────────────────────────
    timer::NetplayClock _clock;
    Metronome* _metronome = nullptr;

    // ─── 構成 ──────────────────────────────────────────
    bool _isHost = false;
    int  _delayFrames = 0;
    int  _maxRollback = 0;
    bool _delayDirty    = false;
    bool _rollbackDirty = false;

    // ─── NTP T1-T4 エコー追跡 ─────────────────────────
    int64_t _lastPeerT1     = 0;
    int64_t _lastPeerRecvUs = 0;

    // ─── 疎通管理 ──────────────────────────────────────
    bool     _peerReady = false;
    int      _framesSinceLastRecv = 0;
    uint32_t _latestPeerFrame = 0;

    // ─── フレーム番号 ────────────────────────────
    uint32_t _currentFrame = 0;
};

} // namespace netplay
} // namespace core
} // namespace cccaster
