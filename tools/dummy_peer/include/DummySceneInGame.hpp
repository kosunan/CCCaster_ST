#pragma once
// ====================================================================
// DummySceneInGame.hpp — 対戦画面の疑似シーン (フェーズC)
//
// 本番 SceneInGame と対応する DummyPeer 側の処理クラス。
// PeerState: IN_GAME
//   Phase 1: intro=2 同期（SYNC_REQ/RES/DONE 交換）
//   Phase 2: GAME_INPUT (11F冗長化) を 60fps で送受信
//            設定ラウンド数消化後 → REMATCH 遷移
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

class DummySceneInGame {
public:
    using SendFunc = std::function<void(const std::vector<uint8_t>&)>;

    explicit DummySceneInGame(const Config& config, bool isHost,
                               SyncResponder& syncResp,
                               SceneStateMachine& sm);

    // ================================================================
    // Update — 毎フレーム呼ばれる（16ms 周期）
    //
    // intro 同期フェーズ: SYNC_REQ/RES 交換 → SYNC_DONE → 開始時刻合わせ
    // 対戦ループ:
    //   - ControllerInputSim::GenerateGameInput() で入力生成
    //   - GAME_INPUT (11F冗長化) パケット送信
    //   - roundTimer 監視 → maxRounds 消化 → REMATCH 遷移
    // ================================================================
    void Update(PeerState state, uint32_t framesInState, SendFunc send);

    // ================================================================
    // HandlePacket — SYNC_REQ/RES/DONE・GAME_INPUT の処理
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

    // intro 同期
    int  _syncRounds   = 0;
    bool _introSynced  = false;
    static constexpr int SYNC_TARGET_ROUNDS = 5;

    // 対戦ループ
    uint32_t _gameFrame     = 0;
    uint32_t _roundsDone    = 0;

    // 入力履歴バッファ (冗長化用)
    static constexpr int HISTORY_SIZE = 11;
    GameInputEntry _inputHistory[HISTORY_SIZE] = {};

    void PushInputHistory(uint16_t dir, uint16_t btn);
};

} // namespace dummy_peer
