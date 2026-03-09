#pragma once
// ====================================================================
// DummySceneLoading.hpp — ロード画面の疑似シーン (フェーズC)
//
// 本番 SceneLoading と対応する DummyPeer 側の処理クラス。
// PeerState: LOADING（Phase1:30F待ち → Phase2:TimeSync → Phase3:ディレイ入力）
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

class DummySceneLoading {
public:
    using SendFunc = std::function<void(const std::vector<uint8_t>&)>;

    explicit DummySceneLoading(const Config& config, bool isHost,
                                SyncResponder& syncResp,
                                SceneStateMachine& sm);

    // ================================================================
    // Update — 毎フレーム呼ばれる（16ms 周期）
    //
    // Phase 1 ( 0〜29F): 安定待ち（入力クリア）
    // Phase 2 (30F〜):   SYNC_REQ/RES 交換 → SYNC_DONE
    //                    ApplySyncOffset → Phase3 開始
    // Phase 3 (SYNC後): LOADING_INPUT パケット送信（6B ペイロード形式）
    //                    RTT ベースのディレイ確定（min=2, max=15）
    //                    バッファ期間終了 → IN_GAME 遷移
    // ================================================================
    void Update(PeerState state, uint32_t framesInState, SendFunc send);

    // ================================================================
    // HandlePacket — SYNC_REQ/RES/DONE・LOADING_INPUT の処理
    // ================================================================
    void HandlePacket(const uint8_t* data, int len, SendFunc send);

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

    // TimeSync
    int  _syncRounds   = 0;
    bool _phase2Done   = false;
    static constexpr int SYNC_TARGET_ROUNDS = 10;

    // Phase 3 ディレイ入力バッファ
    static constexpr int MAX_DELAY = 15;
    uint16_t _localInputRing[MAX_DELAY] = {};
    int      _ringHead      = 0;
    int      _delay         = 0;         // RTT ベースで確定
    int      _delaySyncFrames = 0;
    bool     _phase3Active  = false;
};

} // namespace dummy_peer
