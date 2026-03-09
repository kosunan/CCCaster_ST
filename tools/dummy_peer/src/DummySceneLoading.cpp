#include "DummySceneLoading.hpp"
#include "RealClock.hpp"
#include "SceneStateMachine.hpp"
#include <windows.h>
#include <iostream>
#include <cstring>

namespace dummy_peer {

DummySceneLoading::DummySceneLoading(
    const Config& config, bool isHost,
    SyncResponder& syncResp, SceneStateMachine& sm)
    : _config(config), _isHost(isHost), _syncResp(syncResp), _sm(sm)
{}

int64_t DummySceneLoading::GetNowUs() const {
    return dummy_peer::GetRealQpcTimeUs();
}

void DummySceneLoading::Reset() {
    _seqNum = 0;
    _syncRounds = 0;
    _phase2Done = false;
    _ringHead = 0;
    _delay = 0;
    _delaySyncFrames = 0;
    _phase3Active = false;
    for (int i = 0; i < MAX_DELAY; i++) _localInputRing[i] = 0;
}

void DummySceneLoading::Update(PeerState state, uint32_t framesInState, SendFunc send) {
    (void)state;

    // Phase 1: 安定待ち（30F）
    if (framesInState < 30) {
        return; // 入力クリア状態で待機
    }

    // Phase 2: 時刻同期待ち
    if (!_phase2Done) {
        if (_isHost && _syncRounds < SYNC_TARGET_ROUNDS) {
            static uint32_t syncTimer = 0;
            syncTimer++;
            if (syncTimer % 5 == 1) {
                std::vector<uint8_t> req(9);
                req[0] = 0x10; // SYNC_REQ
                int64_t t1 = GetNowUs();
                std::memcpy(req.data() + 1, &t1, 8);
                send(req);
            }
        }
        // TimeSync 完了判定は HandlePacket から来る SYNC_DONE 受信で行う
        return;
    }

    // Phase 3: ディレイ付き入力交換
    if (!_phase3Active) {
        // RTT ベースのディレイ算出（最初の1回のみ）
        static constexpr int64_t FRAME_US = 16667;
        // ダミーのクロックオフセットからRTTを概算（2*clockOffset）
        int64_t rttUs = _config.minDelayUs + _config.maxDelayUs; // 近似RTT
        int64_t oneWayUs = rttUs / 2;
        _delay = static_cast<int>((oneWayUs + FRAME_US + FRAME_US - 1) / FRAME_US);
        if (_delay < 2)        _delay = 2;
        if (_delay > MAX_DELAY) _delay = MAX_DELAY;
        _phase3Active = true;
        std::cout << "[Loading] Phase3 delay=" << _delay << "F\n";
    }

    // 入力生成
    uint16_t input = _inputSim.GenerateLoadingInput();
    _localInputRing[_ringHead % MAX_DELAY] = input;

    // LOADING_INPUT 送信（CsInputPayload の frame/input フィールドを転用、delayも追加）
    LoadingInputPayload pl{};
    pl.frame = framesInState;
    pl.input = input;
    pl.delay = _delay;

    auto pkt = BuildPacket(Phase::LOADING, PacketType::LOADING_INPUT,
                            NextSeq(), static_cast<uint64_t>(GetNowUs()),
                            pl, PeerState::LOADING);
    send(pkt);

    // バッファ溜め期間
    if (_delaySyncFrames < _delay) {
        _ringHead++;
        _delaySyncFrames++;
        return;
    }

    _ringHead++;
    _delaySyncFrames++;

    // バッファ溜めが完了したらINGAME遷移（バッファが2往復分溜まったら十分）
    if (_delaySyncFrames >= _delay * 2) {
        std::cout << "[Loading] Phase3 done. → IN_GAME\n";
        _sm.TransitionTo(PeerState::IN_GAME);
    }
}

void DummySceneLoading::HandlePacket(const uint8_t* data, int len, SendFunc send) {
    if (len <= 0) return;
    uint8_t type = data[0];

    // SyncResponder で SYNC_REQ → SYNC_RES
    auto resp = _syncResp.HandlePacket(std::vector<uint8_t>(data, data + len));
    if (!resp.empty()) {
        send(resp);
        _syncRounds++;
        if (_syncRounds >= SYNC_TARGET_ROUNDS && !_phase2Done) {
            // SYNC_DONE 送信
            std::vector<uint8_t> done(9);
            done[0] = 0x12;
            int64_t t = GetNowUs();
            std::memcpy(done.data() + 1, &t, 8);
            send(done);
        }
        return;
    }

    // SYNC_DONE 受信
    if (type == 0x12) {
        if (!_phase2Done) {
            _phase2Done = true;
            std::cout << "[Loading] TimeSync done!\n";
        }
        return;
    }
}

} // namespace dummy_peer
