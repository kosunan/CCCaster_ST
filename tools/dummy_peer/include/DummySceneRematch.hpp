#pragma once
// ====================================================================
// DummySceneRematch.hpp — リマッチ画面の疑似シーン (フェーズC)
//
// 本番 SceneRematch（menuConfirmState ゲート方式）と対応する DummyPeer 版。
// PeerState: REMATCH
//   - rematchWaitFrames 間 STATE_REPORT(REMATCH) を定期送信
//   - REMATCH_MENU(menuIndex, confirmed=1) を送信
//   - リモートの REMATCH_MENU を受信
//   - max(local, remote) で遷移先決定
//     → 0(Rematch)=LOADING, 1(CharaSelect)=CS_SELECTING
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

class DummySceneRematch {
public:
    using SendFunc = std::function<void(const std::vector<uint8_t>&)>;

    explicit DummySceneRematch(const Config& config, bool isHost,
                                SyncResponder& syncResp,
                                SceneStateMachine& sm);

    // ================================================================
    // Update — 毎フレーム呼ばれる（16ms 周期）
    //
    //   0〜rematchWaitFrames: 待機
    //   rematchWaitFrames 到達: REMATCH_MENU 送信 (1度のみ)
    //   双方の選択が揃ったら max(local,remote) で遷移先決定
    // ================================================================
    void Update(PeerState state, uint32_t framesInState, SendFunc send);

    // ================================================================
    // HandlePacket — REMATCH_MENU の処理
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

    int8_t _localMenuIndex  = -1;  // -1=未選択
    int8_t _remoteMenuIndex = -1;  // -1=未受信
    bool   _menuSent        = false;

    /// max(local, remote) から遷移先 PeerState を決定
    PeerState DecideNextState();
};

} // namespace dummy_peer
