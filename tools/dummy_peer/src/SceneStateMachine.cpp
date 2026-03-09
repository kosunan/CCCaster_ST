#include "SceneStateMachine.hpp"
#include "RealClock.hpp"
#include "DummySceneCharaSelect.hpp"
#include "DummySceneLoading.hpp"
#include "DummySceneInGame.hpp"
#include "DummySceneRematch.hpp"
#include <windows.h>
#include <iostream>
#include <cstring>

namespace dummy_peer {

SceneStateMachine::SceneStateMachine(const Config& config, bool isHost,
                                      SyncResponder& syncResp)
    : _config(config), _isHost(isHost), _syncResp(syncResp)
{
    _sceneCS      = new DummySceneCharaSelect(config, isHost, syncResp, *this);
    _sceneLoading = new DummySceneLoading(config, isHost, syncResp, *this);
    _sceneInGame  = new DummySceneInGame(config, isHost, syncResp, *this);
    _sceneRematch = new DummySceneRematch(config, isHost, syncResp, *this);
}

SceneStateMachine::~SceneStateMachine() {
    delete _sceneCS;
    delete _sceneLoading;
    delete _sceneInGame;
    delete _sceneRematch;
}

int64_t SceneStateMachine::GetNowUs() const {
    return dummy_peer::GetRealQpcTimeUs();
}

void SceneStateMachine::TransitionTo(PeerState newState) {
    std::cout << "[SSM] Transition: 0x" << std::hex
              << static_cast<int>(_state) << " -> 0x"
              << static_cast<int>(newState) << std::dec
              << " (frames=" << _framesInState << ")\n";
    _state = newState;
    _framesInState = 0;
    _done = (newState == PeerState::REMATCH && _config.maxRounds == 0);
}

void SceneStateMachine::Update(SendFunc send) {
    if (_done) return;

    switch (_state) {
    case PeerState::BOOTING:
    case PeerState::CS_SYNC_WAIT:
    case PeerState::CS_SYNC_DONE:
    case PeerState::CS_SELECTING:
    case PeerState::CS_STAGE_SELECT:
        _sceneCS->Update(_state, _framesInState, send);
        break;

    case PeerState::LOADING:
        _sceneLoading->Update(_state, _framesInState, send);
        break;

    case PeerState::IN_GAME:
        _sceneInGame->Update(_state, _framesInState, send);
        break;

    case PeerState::REMATCH:
        _sceneRematch->Update(_state, _framesInState, send);
        break;
    }

    _framesInState++;
    _totalFrames++;

    // STATE_REPORT 定期送信（60F毎）
    _reportTimer++;
    if (_reportTimer >= REPORT_INTERVAL) {
        _reportTimer = 0;
        SendStateReport(send);
    }
}

void SceneStateMachine::HandlePacket(const uint8_t* data, int len, SendFunc send) {
    if (len <= 0 || data == nullptr) return;

    uint8_t type = data[0];

    // STATE_REPORT 受信 → リモート状態更新
    if (type == static_cast<uint8_t>(PacketType::STATE_REPORT)) {
        UnifiedPacketHeader hdr{};
        if (ParseHeader(data, len, hdr)) {
            _remoteState = static_cast<PeerState>(hdr.peerState);
        }
        return;
    }

    // 各 DummyScene へのディスパッチ
    switch (_state) {
    case PeerState::BOOTING:
    case PeerState::CS_SYNC_WAIT:
    case PeerState::CS_SYNC_DONE:
    case PeerState::CS_SELECTING:
    case PeerState::CS_STAGE_SELECT:
        _sceneCS->HandlePacket(data, len, send);
        break;

    case PeerState::LOADING:
        _sceneLoading->HandlePacket(data, len, send);
        break;

    case PeerState::IN_GAME:
        _sceneInGame->HandlePacket(data, len, send);
        break;

    case PeerState::REMATCH:
        _sceneRematch->HandlePacket(data, len, send);
        break;
    }
}

void SceneStateMachine::SendStateReport(SendFunc send) {
    StateReportPayload pl{};
    pl.peerState       = static_cast<uint8_t>(_state);
    pl.framesInState   = _framesInState;
    pl.configDelay     = 0; // TODO: ctx.delay
    pl.configMaxRollback = 0;
    pl.clockOffsetUs   = _syncResp.GetClockOffset();
    pl.rttUs           = 0;     // TODO: RTT 格納

    auto pkt = BuildPacket(Phase::CHARA_SELECT,
                           PacketType::STATE_REPORT,
                           NextSeq(),
                           static_cast<uint64_t>(GetNowUs()),
                           pl,
                           _state);
    send(pkt);
}

void SceneStateMachine::PrintReport() const {
    std::cout << "[SSM] totalFrames=" << _totalFrames
              << " state=0x" << std::hex << static_cast<int>(_state)
              << std::dec << "\n";
}

} // namespace dummy_peer
