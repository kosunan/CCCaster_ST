#include "DummySceneRematch.hpp"
#include "RealClock.hpp"
#include "SceneStateMachine.hpp"
#include <windows.h>
#include <iostream>
#include <cstring>

namespace dummy_peer {

DummySceneRematch::DummySceneRematch(
    const Config& config, bool isHost,
    SyncResponder& syncResp, SceneStateMachine& sm)
    : _config(config), _isHost(isHost), _syncResp(syncResp), _sm(sm)
{}

int64_t DummySceneRematch::GetNowUs() const {
    return dummy_peer::GetRealQpcTimeUs();
}

void DummySceneRematch::Reset() {
    _seqNum = 0;
    _localMenuIndex  = -1;
    _remoteMenuIndex = -1;
    _menuSent        = false;
}

// ================================================================
// DecideNextState — max(local, remote) で遷移先決定
//   menuIndex 0 → LOADING（再戦）
//   menuIndex 1 → CS_SELECTING（キャラセレ戻り）
// ================================================================
PeerState DummySceneRematch::DecideNextState() {
    int8_t final_idx = (_localMenuIndex > _remoteMenuIndex)
                     ? _localMenuIndex
                     : _remoteMenuIndex;
    // クランプ
    if (final_idx > 1) final_idx = 1;
    if (final_idx < 0) final_idx = 0;

    std::cout << "[Rematch] Final: local=" << (int)_localMenuIndex
              << " remote=" << (int)_remoteMenuIndex
              << " → " << (final_idx == 0 ? "LOADING(Rematch)" : "CS_SELECTING")
              << "\n";

    return (final_idx == 0) ? PeerState::LOADING : PeerState::CS_SELECTING;
}

void DummySceneRematch::Update(PeerState state, uint32_t framesInState, SendFunc send) {
    (void)state;

    // rematchWaitFrames 後に選択を送信（1度のみ）
    if (!_menuSent && framesInState >= static_cast<uint32_t>(_config.rematchWaitFrames)) {
        _localMenuIndex = _inputSim.GenerateRematchSelection(_config.rematchChoice);
        _menuSent = true;

        // REMATCH_MENU パケット送信
        RematchMenuPayload pl{};
        pl.menuIndex = _localMenuIndex;
        pl.confirmed = 1;
        auto pkt = BuildPacket(Phase::REMATCH, PacketType::REMATCH_MENU,
                                NextSeq(), static_cast<uint64_t>(GetNowUs()),
                                pl, PeerState::REMATCH);
        send(pkt);
        std::cout << "[Rematch] Sent menuIndex=" << (int)_localMenuIndex << "\n";
    }

    // 双方の選択が揃ったら遷移
    if (_localMenuIndex != -1 && _remoteMenuIndex != -1) {
        PeerState next = DecideNextState();
        _sm.TransitionTo(next);
    }

    // タイムアウト: 300F(5秒)待っても揃わない場合はリマッチ扱い
    if (_menuSent && _remoteMenuIndex == -1 &&
        framesInState >= static_cast<uint32_t>(_config.rematchWaitFrames) + 300) {
        std::cout << "[Rematch] TIMEOUT: forcing LOADING\n";
        _sm.TransitionTo(PeerState::LOADING);
    }
}

void DummySceneRematch::HandlePacket(const uint8_t* data, int len, SendFunc send) {
    (void)send;
    if (len <= 0) return;

    UnifiedPacketHeader hdr{};
    if (!ParseHeader(data, len, hdr)) return;
    if (hdr.type != static_cast<uint8_t>(PacketType::REMATCH_MENU)) return;

    RematchMenuPayload pl{};
    if (ParsePayload<RematchMenuPayload>(data, len, pl)) {
        _remoteMenuIndex = pl.menuIndex;
        std::cout << "[Rematch] Remote selected: menuIndex=" << (int)_remoteMenuIndex << "\n";
    }
}

} // namespace dummy_peer
