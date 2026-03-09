#include "DummySceneCharaSelect.hpp"
#include "RealClock.hpp"
#include "SceneStateMachine.hpp"
#include <windows.h>
#include <iostream>
#include <cstring>

namespace dummy_peer {

DummySceneCharaSelect::DummySceneCharaSelect(
    const Config& config, bool isHost,
    SyncResponder& syncResp, SceneStateMachine& sm)
    : _config(config), _isHost(isHost), _syncResp(syncResp), _sm(sm)
{}

int64_t DummySceneCharaSelect::GetNowUs() const {
    return dummy_peer::GetRealQpcTimeUs();
}

void DummySceneCharaSelect::Reset() {
    _seqNum = 0;
    _syncRounds = 0;
    _syncDoneSent = false;
    _syncDoneRecv = false;
    _latestRemoteInput = 0;
}

// ================================================================
// HandleBooting — Phase BOOTING: 50F カウント → CS_SYNC_WAIT
// ================================================================
void DummySceneCharaSelect::HandleBooting(uint32_t framesInState, SendFunc send) {
    (void)send;
    if (framesInState >= 50) {
        std::cout << "[CharaSelect] BOOTING done (50F). → CS_SYNC_WAIT\n";
        _sm.TransitionTo(PeerState::CS_SYNC_WAIT);
    }
}

// ================================================================
// HandleCsSyncWait — Phase CS_SYNC_WAIT: SYNC_REQ → SYNC_RES 交換
//
// 当 DummyPeer が Host 側の場合は SYNC_REQ/RES を能動的に送信する。
// Client 側の場合は SYNC_REQ の受信待ちを行う。
// ================================================================
void DummySceneCharaSelect::HandleCsSyncWait(uint32_t framesInState, SendFunc send) {
    (void)framesInState;

    // Host 側なら SYNC_REQ を 5F ごとに送信
    if (_isHost && (_syncRounds < SYNC_TARGET_ROUNDS)) {
        static uint32_t syncReqTimer = 0;
        syncReqTimer++;
        if (syncReqTimer % 5 == 1) {
            // SYNC_REQ: [0x10][T1(8)] = 9 bytes
            std::vector<uint8_t> req(9);
            req[0] = 0x10; // SYNC_REQ (SyncResponder 互換)
            int64_t t1 = GetNowUs();
            std::memcpy(req.data() + 1, &t1, 8);
            send(req);
        }
    }

    // SYNC_DONE が揃ったら CS_SYNC_DONE へ
    if (_syncDoneSent && _syncDoneRecv) {
        std::cout << "[CharaSelect] TimeSync done (rounds=" << _syncRounds << "). → CS_SYNC_DONE\n";
        _sm.TransitionTo(PeerState::CS_SYNC_DONE);
    }
}

// ================================================================
// HandleCsSyncDone — ClockOffset 適用 → 即座に CS_SELECTING
// ================================================================
void DummySceneCharaSelect::HandleCsSyncDone(SendFunc send) {
    (void)send;
    std::cout << "[CharaSelect] CS_SYNC_DONE: clockOffset="
              << _syncResp.GetClockOffset() << "us → CS_SELECTING\n";
    _sm.TransitionTo(PeerState::CS_SELECTING);
}

// ================================================================
// HandleCsSelecting — CS_INPUT 送受信
// ================================================================
void DummySceneCharaSelect::HandleCsSelecting(uint32_t framesInState, SendFunc send) {
    // 入力生成（Filter A/C 互換シーケンス）
    uint16_t input = _inputSim.GenerateCharaSelectInput(framesInState);

    // CS_INPUT パケット送信（CsInputPayload 形式、20B hdr + 6B payload）
    CsInputPayload pl{};
    pl.frame = framesInState;
    pl.input = input;

    auto pkt = BuildPacket(Phase::CHARA_SELECT, PacketType::CS_INPUT,
                            NextSeq(), static_cast<uint64_t>(GetNowUs()),
                            pl, PeerState::CS_SELECTING);
    send(pkt);

    // charaSelectFrames 経過 → CS_STAGE_SELECT
    if (framesInState >= static_cast<uint32_t>(_config.charaSelectFrames)) {
        std::cout << "[CharaSelect] CS_SELECTING done (" << framesInState << "F). → CS_STAGE_SELECT\n";
        _sm.TransitionTo(PeerState::CS_STAGE_SELECT);
    }
}

// ================================================================
// HandleCsStageSelect — ステージ選択 → LOADING
// ================================================================
void DummySceneCharaSelect::HandleCsStageSelect(uint32_t framesInState, SendFunc send) {
    // CS_INPUT でステージ確定入力を送信
    CsInputPayload pl{};
    pl.frame = framesInState;
    pl.input = ControllerInputSim::BTN_A | ControllerInputSim::BTN_CONFIRM;
    auto pkt = BuildPacket(Phase::CHARA_SELECT, PacketType::CS_INPUT,
                            NextSeq(), static_cast<uint64_t>(GetNowUs()),
                            pl, PeerState::CS_STAGE_SELECT);
    send(pkt);

    if (framesInState >= static_cast<uint32_t>(_config.stageSelectFrames)) {
        std::cout << "[CharaSelect] Stage selected. → LOADING\n";
        _sm.TransitionTo(PeerState::LOADING);
    }
}

void DummySceneCharaSelect::Update(PeerState state, uint32_t framesInState, SendFunc send) {
    switch (state) {
    case PeerState::BOOTING:         HandleBooting(framesInState, send);     break;
    case PeerState::CS_SYNC_WAIT:    HandleCsSyncWait(framesInState, send);  break;
    case PeerState::CS_SYNC_DONE:    HandleCsSyncDone(send);                 break;
    case PeerState::CS_SELECTING:    HandleCsSelecting(framesInState, send); break;
    case PeerState::CS_STAGE_SELECT: HandleCsStageSelect(framesInState, send); break;
    default: break;
    }
}

void DummySceneCharaSelect::HandlePacket(const uint8_t* data, int len, SendFunc send) {
    if (len <= 0) return;
    uint8_t type = data[0];

    // SyncResponder で SYNC_REQ → SYNC_RES
    auto resp = _syncResp.HandlePacket(std::vector<uint8_t>(data, data + len));
    if (!resp.empty()) {
        send(resp);
        _syncRounds++;
        // SYNC_DONE を送信（10往復完了後）
        if (_syncRounds >= SYNC_TARGET_ROUNDS && !_syncDoneSent) {
            std::vector<uint8_t> done(9);
            done[0] = 0x12; // SYNC_DONE
            int64_t t = GetNowUs();
            std::memcpy(done.data() + 1, &t, 8);
            send(done);
            _syncDoneSent = true;
        }
        return;
    }

    // SYNC_DONE 受信
    if (type == 0x12) {
        _syncDoneRecv = true;
        return;
    }

    // CS_INPUT 受信
    if (len >= static_cast<int>(sizeof(UnifiedPacketHeader) + sizeof(CsInputPayload))) {
        UnifiedPacketHeader hdr{};
        if (ParseHeader(data, len, hdr) && hdr.type == static_cast<uint8_t>(PacketType::CS_INPUT)) {
            CsInputPayload pl{};
            ParsePayload<CsInputPayload>(data, len, pl);
            _latestRemoteInput = pl.input;
        }
    }
}

} // namespace dummy_peer
