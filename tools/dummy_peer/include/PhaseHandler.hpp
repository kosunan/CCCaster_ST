#pragma once
#include "UnifiedProtocol.hpp"
#include <cstdint>
#include <vector>
#include <chrono>
#include <functional>
#include <iostream>

namespace dummy_peer {

/**
 * フェーズ別パケット送受信ハンドラ。
 * Phase 1.5 (キャラセレ同期) → Phase 2 (キャラセレ入力) → Phase 4 (対戦前同期)
 * のフルサイクルを処理する。
 */
class PhaseHandler {
public:
    // パケット送信コールバック (送信バッファ, サイズ)
    using SendFunc = std::function<void(const std::vector<uint8_t>&)>;

    explicit PhaseHandler(bool isHost);

    // --- パケット受信処理 ---
    // 受信したバイト列を解析し、内部ステートを更新する。
    // 応答パケットがあれば sendFunc で送信する。
    void HandlePacket(const uint8_t* data, int len, SendFunc sendFunc);

    // --- 能動的パケット生成 ---
    // 現在のフェーズに応じたパケットを生成する (ポーリングで呼ぶ)
    std::vector<uint8_t> GeneratePacket();

    // --- フェーズ制御 ---
    Phase GetCurrentPhase() const { return _currentPhase; }
    void SetPhase(Phase p) { _currentPhase = p; }

    // --- Phase 1.5: キャラセレ同期 ---
    bool IsOpponentSyncReady() const { return _opponentSyncReady; }
    bool IsTimeSyncComplete() const { return _timeSyncComplete; }

    // --- Phase 4: 対戦前同期 ---
    bool IsRngSynced() const { return _rngSynced; }
    bool IsOpponentReady() const { return _opponentReady; }

    // --- 統計 ---
    uint64_t GetPacketsSent() const { return _packetsSent; }
    uint64_t GetPacketsReceived() const { return _packetsReceived; }
    double   GetEstimatedThetaUs() const { return _estimatedThetaUs; }
    int      GetTimeSyncRoundsCompleted() const { return _timeSyncRounds; }

    void PrintReport() const;

private:
    bool _isHost;
    Phase _currentPhase = Phase::CHARA_SELECT;
    uint16_t _seqNum = 0;

    // Phase 1.5
    bool _opponentSyncReady = false;
    bool _timeSyncComplete = false;
    int  _timeSyncRounds = 0;
    static constexpr int TIME_SYNC_TARGET_ROUNDS = 10;

    // TIME_SYNC θ推定
    double _estimatedThetaUs = 0.0;
    double _thetaSum = 0.0;

    // Phase 2
    uint32_t _localFrame = 0;
    uint16_t _lastRemoteInput = 0;

    // Phase 4
    bool _rngSynced = false;
    bool _localReady = false;
    bool _opponentReady = false;

    // 統計
    uint64_t _packetsSent = 0;
    uint64_t _packetsReceived = 0;

    uint64_t GetNowUs() const;
    uint16_t NextSeq() { return _seqNum++; }

    // 内部ハンドラ
    void HandleCsSyncReady(const uint8_t* data, int len, SendFunc sendFunc);
    void HandleTimeSyncReq(const uint8_t* data, int len, SendFunc sendFunc);
    void HandleTimeSyncRes(const uint8_t* data, int len);
    void HandleCsInput(const uint8_t* data, int len);
    void HandleRngSync(const uint8_t* data, int len);
    void HandleReady(const uint8_t* data, int len);
};

} // namespace dummy_peer
