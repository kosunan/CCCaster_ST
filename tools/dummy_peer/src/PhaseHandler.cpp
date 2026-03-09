#include "PhaseHandler.hpp"
#include "RealClock.hpp"
#include <cmath>

namespace dummy_peer {

PhaseHandler::PhaseHandler(bool isHost) : _isHost(isHost) {}

uint64_t PhaseHandler::GetNowUs() const {
    return static_cast<uint64_t>(dummy_peer::GetRealQpcTimeUs());
}

// ===================================================================
//  受信パケットのディスパッチ
// ===================================================================
void PhaseHandler::HandlePacket(const uint8_t* data, int len, SendFunc sendFunc) {
    UnifiedPacketHeader hdr{};
    if (!ParseHeader(data, len, hdr)) return;

    _packetsReceived++;
    PacketType ptype = static_cast<PacketType>(hdr.type);

    switch (ptype) {
        case PacketType::CS_SYNC_READY:   HandleCsSyncReady(data, len, sendFunc); break;
        case PacketType::TIME_SYNC_REQ:   HandleTimeSyncReq(data, len, sendFunc); break;
        case PacketType::TIME_SYNC_RES:   HandleTimeSyncRes(data, len); break;
        case PacketType::CS_INPUT:        HandleCsInput(data, len); break;
        case PacketType::RNG_SYNC:        HandleRngSync(data, len); break;
        case PacketType::READY:           HandleReady(data, len); break;
        default: break;
    }
}

// ===================================================================
//  能動的パケット生成 (ポーリングで呼ぶ)
// ===================================================================
std::vector<uint8_t> PhaseHandler::GeneratePacket() {
    uint64_t now = GetNowUs();

    switch (_currentPhase) {
        case Phase::CHARA_SELECT: {
            // Phase 1.5: CS_SYNC_READY を送信
            if (!_timeSyncComplete) {
                CsSyncReadyPayload pl{};
                pl.frameCount = 200;
                auto pkt = BuildPacket(Phase::CHARA_SELECT, PacketType::CS_SYNC_READY,
                                       NextSeq(), now, pl);
                _packetsSent++;
                return pkt;
            }
            // Phase 2: CS_INPUT を送信 (ランダム入力)
            CsInputPayload pl{};
            pl.frame = _localFrame++;
            // ランダムボタン生成 (方向4bit + ボタン)
            pl.input = static_cast<uint16_t>(rand() & 0x0FFF);
            auto pkt = BuildPacket(Phase::CHARA_SELECT, PacketType::CS_INPUT,
                                   NextSeq(), now, pl);
            _packetsSent++;
            return pkt;
        }

        case Phase::PRE_GAME_SYNC: {
            // Phase 4: Host は RNG_SYNC、両者は READY
            if (_isHost && !_rngSynced) {
                RngSyncPayload pl{};
                pl.rngState0 = 0x12345678;
                pl.rngState1 = 0x9ABCDEF0;
                pl.rngState2 = 0x13579BDF;
                memset(pl.rngArray, 0xAA, sizeof(pl.rngArray));
                _rngSynced = true; // Host は送信した時点で自身を同期済みに
                auto pkt = BuildPacket(Phase::PRE_GAME_SYNC, PacketType::RNG_SYNC,
                                       NextSeq(), now, pl);
                _packetsSent++;
                return pkt;
            }
            // RNG 同期済みなら READY を繰り返し送信 (相手が受信するまで)
            if (_rngSynced) {
                _localReady = true;
                ReadyPayload pl{};
                pl.ready = 1;
                auto pkt = BuildPacket(Phase::PRE_GAME_SYNC, PacketType::READY,
                                       NextSeq(), now, pl);
                _packetsSent++;
                return pkt;
            }
            // HEARTBEAT
            return BuildHeaderOnlyPacket(Phase::PRE_GAME_SYNC, PacketType::HEARTBEAT,
                                         NextSeq(), now);
        }

        default:
            return BuildHeaderOnlyPacket(_currentPhase, PacketType::HEARTBEAT,
                                         NextSeq(), now);
    }
}

// ===================================================================
//  個別ハンドラ
// ===================================================================

void PhaseHandler::HandleCsSyncReady(const uint8_t* data, int len, SendFunc sendFunc) {
    CsSyncReadyPayload pl{};
    if (ParsePayload(data, len, pl)) {
        _opponentSyncReady = true;
        std::cout << "[PhaseHandler] Opponent reached frame " << pl.frameCount
                  << " (CS_SYNC_READY)\n";

        // 双方到達 → 時刻同期開始 (Client が TIME_SYNC_REQ を送信)
        if (_opponentSyncReady && !_isHost && _timeSyncRounds < TIME_SYNC_TARGET_ROUNDS) {
            TimeSyncReqPayload req{};
            req.t1 = static_cast<int64_t>(GetNowUs());
            auto pkt = BuildPacket(Phase::CHARA_SELECT, PacketType::TIME_SYNC_REQ,
                                   NextSeq(), GetNowUs(), req);
            sendFunc(pkt);
            _packetsSent++;
        }
    }
}

void PhaseHandler::HandleTimeSyncReq(const uint8_t* data, int len, SendFunc sendFunc) {
    TimeSyncReqPayload req{};
    if (!ParsePayload(data, len, req)) return;

    uint64_t t2 = GetNowUs();
    TimeSyncResPayload res{};
    res.t1 = req.t1;
    res.t2 = static_cast<int64_t>(t2);
    res.t3 = static_cast<int64_t>(GetNowUs());

    auto pkt = BuildPacket(_currentPhase, PacketType::TIME_SYNC_RES,
                           NextSeq(), GetNowUs(), res);
    sendFunc(pkt);
    _packetsSent++;

    // Host側も応答回数で同期完了を判定
    _timeSyncRounds++;
    if (_timeSyncRounds >= TIME_SYNC_TARGET_ROUNDS && !_timeSyncComplete) {
        _timeSyncComplete = true;
        std::cout << "[PhaseHandler] Host time sync complete (" << _timeSyncRounds << " responses sent)\n";
    }
}

void PhaseHandler::HandleTimeSyncRes(const uint8_t* data, int len) {
    TimeSyncResPayload res{};
    if (!ParsePayload(data, len, res)) return;

    int64_t t4 = static_cast<int64_t>(GetNowUs());

    // NTP方式: θ = ((t2 - t1) + (t3 - t4)) / 2
    double theta = (static_cast<double>(res.t2 - res.t1) +
                    static_cast<double>(res.t3 - t4)) / 2.0;
    _thetaSum += theta;
    _timeSyncRounds++;

    if (_timeSyncRounds >= TIME_SYNC_TARGET_ROUNDS) {
        _estimatedThetaUs = _thetaSum / _timeSyncRounds;
        _timeSyncComplete = true;
        std::cout << "[PhaseHandler] Time sync complete: theta = "
                  << _estimatedThetaUs / 1000.0 << " ms ("
                  << _timeSyncRounds << " rounds)\n";
    }
}

void PhaseHandler::HandleCsInput(const uint8_t* data, int len) {
    CsInputPayload pl{};
    if (ParsePayload(data, len, pl)) {
        _lastRemoteInput = pl.input;
    }
}

void PhaseHandler::HandleRngSync(const uint8_t* data, int len) {
    RngSyncPayload pl{};
    if (ParsePayload(data, len, pl)) {
        _rngSynced = true;
        std::cout << "[PhaseHandler] RNG synced: state0=0x" << std::hex << pl.rngState0
                  << " state1=0x" << pl.rngState1
                  << " state2=0x" << pl.rngState2 << std::dec << "\n";
    }
}

void PhaseHandler::HandleReady(const uint8_t* data, int len) {
    ReadyPayload pl{};
    if (ParsePayload(data, len, pl)) {
        if (pl.ready == 1) {
            _opponentReady = true;
            std::cout << "[PhaseHandler] Opponent is READY!\n";
        }
    }
}

// ===================================================================
//  レポート
// ===================================================================
void PhaseHandler::PrintReport() const {
    auto phaseStr = [](Phase p) -> const char* {
        switch(p) {
            case Phase::NEGOTIATION: return "NEGOTIATION";
            case Phase::CHARA_SELECT: return "CHARA_SELECT";
            case Phase::LOADING: return "LOADING";
            case Phase::PRE_GAME_SYNC: return "PRE_GAME_SYNC";
            case Phase::IN_GAME: return "IN_GAME";
            case Phase::REMATCH: return "REMATCH";
            default: return "UNKNOWN";
        }
    };
    std::cout << "  --- Phase Handler Report ---\n";
    std::cout << "    Role:             " << (_isHost ? "HOST" : "CLIENT") << "\n";
    std::cout << "    Current Phase:    " << phaseStr(_currentPhase) << "\n";
    std::cout << "    Packets Sent:     " << _packetsSent << "\n";
    std::cout << "    Packets Received: " << _packetsReceived << "\n";
    std::cout << "    Opponent Sync:    " << (_opponentSyncReady ? "Yes" : "No") << "\n";
    std::cout << "    Time Sync:        " << (_timeSyncComplete ? "Complete" : "Pending")
              << " (" << _timeSyncRounds << "/" << TIME_SYNC_TARGET_ROUNDS << " rounds)\n";
    std::cout << "    Theta:            " << _estimatedThetaUs / 1000.0 << " ms\n";
    std::cout << "    RNG Synced:       " << (_rngSynced ? "Yes" : "No") << "\n";
    std::cout << "    Opponent Ready:   " << (_opponentReady ? "Yes" : "No") << "\n";
}

} // namespace dummy_peer
