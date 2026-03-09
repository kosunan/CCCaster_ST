#pragma once
// ====================================================================
// SceneStateMachine.hpp — 疑似画面遷移エンジン (フェーズC)
//
// 本番 DLL の SceneRunner::Run() に対応する DummyPeer 版ステートマシン。
// PeerState の管理と各 DummyScene へのディスパッチを担当する。
// ====================================================================

#include "UnifiedProtocol.hpp"
#include "SyncResponder.hpp"
#include "DummyPeer.hpp"
#include <cstdint>
#include <functional>
#include <vector>
#include <string>

// フォワード宣言（循環参照回避）
namespace dummy_peer {
class DummySceneCharaSelect;
class DummySceneLoading;
class DummySceneInGame;
class DummySceneRematch;
}

namespace dummy_peer {

class SceneStateMachine {
public:
    /// パケット送信コールバック
    using SendFunc = std::function<void(const std::vector<uint8_t>&)>;

    explicit SceneStateMachine(const Config& config, bool isHost,
                                SyncResponder& syncResp);
    ~SceneStateMachine();

    // ================================================================
    // Update — メインループから 16ms 周期で呼ばれる
    //
    // 現在の PeerState に対応する DummyScene::Update() を呼び出す。
    // STATE_REPORT の定期送信も行う（60F 毎）。
    // ================================================================
    void Update(SendFunc send);

    // ================================================================
    // HandlePacket — 受信パケットを PeerState に応じてディスパッチ
    // ================================================================
    void HandlePacket(const uint8_t* data, int len, SendFunc send);

    // === 状態アクセス ===
    PeerState GetState()          const { return _state; }
    PeerState GetRemoteState()    const { return _remoteState; }
    uint32_t  GetFramesInState()  const { return _framesInState; }
    bool      IsDone()            const { return _done; }

    // === 状態遷移（DummyScene から呼ばれる） ===
    void TransitionTo(PeerState newState);

    // === レポート ===
    void PrintReport() const;

private:
    Config         _config;
    bool           _isHost;
    SyncResponder& _syncResp;

    PeerState  _state        = PeerState::BOOTING;
    PeerState  _remoteState  = PeerState::BOOTING;
    uint32_t   _framesInState = 0;
    uint32_t   _totalFrames   = 0;
    bool       _done          = false;

    // 定期 STATE_REPORT 送信カウンタ
    uint32_t   _reportTimer   = 0;
    static constexpr uint32_t REPORT_INTERVAL = 60;

    uint16_t _seqNum = 0;
    uint16_t NextSeq() { return _seqNum++; }

    int64_t GetNowUs() const;

    // 各 DummyScene インスタンス（前方宣言済み）
    DummySceneCharaSelect* _sceneCS      = nullptr;
    DummySceneLoading*     _sceneLoading = nullptr;
    DummySceneInGame*      _sceneInGame  = nullptr;
    DummySceneRematch*     _sceneRematch = nullptr;

    void SendStateReport(SendFunc send);
};

} // namespace dummy_peer
