#pragma once
// ====================================================================
// DummySceneCharaSelect.hpp — キャラセレクト画面の疑似シーン (フェーズC)
//
// 本番 SceneCharaSelect と対応する DummyPeer 側の処理クラス。
// PeerState: BOOTING → CS_SYNC_WAIT → CS_SYNC_DONE → CS_SELECTING → CS_STAGE_SELECT
// ====================================================================

#include "UnifiedProtocol.hpp"
#include "SyncResponder.hpp"
#include "ControllerInputSim.hpp"
#include "DummyPeer.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace dummy_peer {

class SceneStateMachine;

class DummySceneCharaSelect {
public:
    using SendFunc = std::function<void(const std::vector<uint8_t>&)>;

    explicit DummySceneCharaSelect(const Config& config, bool isHost,
                                    SyncResponder& syncResp,
                                    SceneStateMachine& sm);

    // ================================================================
    // Update — 毎フレーム呼ばれる（16ms 周期）
    //
    // PeerState に応じた処理を行う:
    //   BOOTING:         50F カウント → CS_SYNC_WAIT 遷移
    //   CS_SYNC_WAIT:    SYNC_REQ/RES 交換 → SYNC_DONE → CS_SYNC_DONE
    //   CS_SYNC_DONE:    ClockOffset 適用 → 即座に CS_SELECTING
    //   CS_SELECTING:    CS_INPUT 送信 (FilterA/B 付き、6B ペイロード形式)
    //                    charaSelectFrames 経過 → CS_STAGE_SELECT
    //   CS_STAGE_SELECT: ステージ選択入力 → LOADING 遷移
    // ================================================================
    void Update(PeerState state, uint32_t framesInState, SendFunc send);

    // ================================================================
    // HandlePacket — 受信パケットの処理
    //
    //   SYNC_REQ  → SyncResponder で SYNC_RES 送信
    //   SYNC_RES  → θ算出
    //   SYNC_DONE → CS_SYNC_DONE 遷移フラグ
    //   CS_INPUT  → リモート入力記録
    // ================================================================
    void HandlePacket(const uint8_t* data, int len, SendFunc send);

    // リモート入力最新値の取得
    uint16_t GetLatestRemoteInput() const { return _latestRemoteInput; }

    void Reset();

private:
    Config           _config;
    bool             _isHost;
    SyncResponder&   _syncResp;
    SceneStateMachine& _sm;
    ControllerInputSim _inputSim;

    uint16_t _seqNum = 0;
    uint16_t NextSeq() { return _seqNum++; }
    int64_t  GetNowUs() const;

    // SYNC 状態
    int  _syncRounds     = 0;
    bool _syncDoneSent   = false;
    bool _syncDoneRecv   = false;
    static constexpr int SYNC_TARGET_ROUNDS = 10;

    // CS_INPUT 受信
    uint16_t _latestRemoteInput = 0;

    // === Phase別メソッド ===
    void HandleBooting(uint32_t framesInState, SendFunc send);
    void HandleCsSyncWait(uint32_t framesInState, SendFunc send);
    void HandleCsSyncDone(SendFunc send);
    void HandleCsSelecting(uint32_t framesInState, SendFunc send);
    void HandleCsStageSelect(uint32_t framesInState, SendFunc send);
};

} // namespace dummy_peer
