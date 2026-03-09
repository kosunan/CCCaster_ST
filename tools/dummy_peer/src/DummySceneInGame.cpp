#include "DummySceneInGame.hpp"
#include "RealClock.hpp"
#include "SceneStateMachine.hpp"
#include <windows.h>
#include <iostream>
#include <cstring>

namespace dummy_peer {

DummySceneInGame::DummySceneInGame(
    const Config& config, bool isHost,
    SyncResponder& syncResp, SceneStateMachine& sm)
    : _config(config), _isHost(isHost), _syncResp(syncResp), _sm(sm)
{}

int64_t DummySceneInGame::GetNowUs() const {
    return _syncResp.GetLocalTimeUs();
}

void DummySceneInGame::Reset() {
    _seqNum = 0;
    _syncRounds = 0;
    _introSynced = false;
    _gameFrame = 0;
    _roundsDone = 0;
    for (int i = 0; i < HISTORY_SIZE; i++) _inputHistory[i] = {};
}

void DummySceneInGame::PushInputHistory(uint16_t dir, uint16_t btn) {
    // リングバッファのシフト（[0]=最新→[10]=10F前）
    for (int i = HISTORY_SIZE - 1; i > 0; i--) {
        _inputHistory[i] = _inputHistory[i-1];
    }
    _inputHistory[0].direction = dir;
    _inputHistory[0].buttons   = btn;
}

void DummySceneInGame::Update(PeerState state, uint32_t framesInState, SendFunc send) {
    (void)state;

    // Phase 1: intro=2 同期（最初の5往復 SYNC_REQ/RES）
    if (!_introSynced) {
        if (_isHost && _syncRounds < SYNC_TARGET_ROUNDS) {
            static uint32_t syncTimer = 0;
            syncTimer++;
            if (syncTimer % 3 == 1) {
                std::vector<uint8_t> req(9);
                req[0] = 0x10;
                int64_t t1 = GetNowUs();
                std::memcpy(req.data() + 1, &t1, 8);
                send(req);
            }
        }
        return;
    }

    // Phase 2: 対戦ループ（GAME_INPUT 11F冗長化）
    uint32_t rawInput = _inputSim.GenerateGameInput(_gameFrame);
    uint16_t dir = static_cast<uint16_t>((rawInput >> 16) & 0xFFFF);
    uint16_t btn = static_cast<uint16_t>(rawInput & 0xFFFF);
    PushInputHistory(dir, btn);

    GameInputPayload pl{};
    pl.latestFrame = _gameFrame;
    pl.inputDelay  = static_cast<uint8_t>(_config.rollupSpeed);
    pl.roundTimer  = _gameFrame;
    pl.wasapiClock = static_cast<uint64_t>(GetNowUs());
    for (int i = 0; i < HISTORY_SIZE; i++) {
        pl.history[i] = _inputHistory[i];
    }

    auto pkt = BuildPacket(Phase::IN_GAME, PacketType::GAME_INPUT,
                            NextSeq(), static_cast<uint64_t>(GetNowUs()),
                            pl, PeerState::IN_GAME);
    send(pkt);

    _gameFrame++;

    // ラウンド終了判定（設定フレーム数経過でラウンド消化）
    if (static_cast<int>(_gameFrame) >= _config.roundFrames) {
        _gameFrame = 0;
        _roundsDone++;
        std::cout << "[InGame] Round " << _roundsDone << " done.\n";

        if (_roundsDone >= static_cast<uint32_t>(_config.maxRounds)) {
            std::cout << "[InGame] All rounds done. → REMATCH\n";
            _sm.TransitionTo(PeerState::REMATCH);
        }
    }
}

void DummySceneInGame::HandlePacket(const uint8_t* data, int len, SendFunc send) {
    if (len <= 0) return;

    // SyncResponder で SYNC_REQ → SYNC_RES
    auto resp = _syncResp.HandlePacket(std::vector<uint8_t>(data, data + len));
    if (!resp.empty()) {
        send(resp);
        _syncRounds++;
        if (_syncRounds >= SYNC_TARGET_ROUNDS && !_introSynced) {
            std::vector<uint8_t> done(9);
            done[0] = 0x12;
            int64_t t = GetNowUs();
            std::memcpy(done.data() + 1, &t, 8);
            send(done);
        }
        return;
    }

    uint8_t type = data[0];
    if (type == 0x12) {
        if (!_introSynced) {
            _introSynced = true;
            std::cout << "[InGame] intro sync done!\n";
        }
        return;
    }
}

} // namespace dummy_peer
