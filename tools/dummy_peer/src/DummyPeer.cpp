#include "DummyPeer.hpp"
#include "RealClock.hpp"
#include "NegotiationTest.hpp"
#include "SyncTest.hpp"
#include "FullTest.hpp"
#include "GameTest.hpp"
#include "E2ETest.hpp"
#include "SyncResponder.hpp"
#include "NegotiationResponder.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <memory>

namespace dummy_peer {

DummyPeer::DummyPeer(const Config& config) : _config(config) {}

DummyPeer::~DummyPeer() {
    CloseSocket();
}

void DummyPeer::InitSocket() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // ノンブロッキングに設定
    u_long mode = 1;
    ioctlsocket(static_cast<SOCKET>(_socket), FIONBIO, &mode);

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(_config.localPort);
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bind(static_cast<SOCKET>(_socket), (sockaddr*)&bindAddr, sizeof(bindAddr));

    std::cout << "[DummyPeer] Socket bound to port " << _config.localPort << "\n";
}

void DummyPeer::CloseSocket() {
    if (_socket) {
        closesocket(static_cast<SOCKET>(_socket));
        _socket = 0;
        WSACleanup();
    }
}

void DummyPeer::Stop() {
    _running = false;
}

void DummyPeer::Run() {
    InitSocket();
    _running = true;

    auto testModeStr = [](TestMode m) -> const char* {
        switch(m) {
            case TestMode::Negotiation: return "NEGOTIATION";
            case TestMode::Sync:        return "SYNC";
            case TestMode::Full:        return "FULL";
            case TestMode::Game:        return "GAME";
            case TestMode::E2E:         return "E2E";
        }
        return "UNKNOWN";
    };

    std::cout << "=== DummyPeer Configuration ===\n";
    std::cout << "  Mode:         " << (_config.mode == PeerMode::Host ? "HOST" : "CLIENT") << "\n";
    std::cout << "  Test Mode:    " << testModeStr(_config.testMode) << "\n";
    std::cout << "  Target:       " << _config.targetIp << ":" << _config.remotePort << "\n";
    std::cout << "  Local Port:   " << _config.localPort << "\n";
    if (_config.testMode != TestMode::Negotiation) {
        std::cout << "  Delay:        " << _config.minDelayUs/1000 << "ms ~ " << _config.maxDelayUs/1000 << "ms\n";
        std::cout << "  Packet Loss:  " << (_config.packetLossRate * 100) << "%\n";
        if (_config.mode == PeerMode::Host) {
            std::cout << "  Clock Offset: " << _config.clockOffsetUs << " us (HOST 基準時計)\n";
            std::cout << "  Clock Drift:  " << _config.clockDriftPpm << " ppm\n";
        } else {
            std::cout << "  Sync Pings:   30 (自動SYNC_REQ送信)\n";
        }
        std::cout << "  Game Speed:   " << _config.minFrameTimeMs << "ms ~ " << _config.maxFrameTimeMs << "ms per frame\n";
    }
    std::cout << "  Duration:     " << _config.testDurationSec << " sec\n";
    if (_config.disconnectAtSec >= 0) {
        std::cout << "  Disconnect:   at " << _config.disconnectAtSec << "s, reconnect after " << _config.reconnectAfterSec << "s\n";
    }
    if (_config.testMode == TestMode::E2E) {
        std::cout << "  [E2E] RoundFrames:        " << _config.roundFrames << "\n";
        std::cout << "  [E2E] MaxRounds:          " << _config.maxRounds << "\n";
        std::cout << "  [E2E] RematchChoice:      " << _config.rematchChoice << " (0=Rematch,1=CharaSelect)\n";
        std::cout << "  [E2E] CharaSelectFrames:  " << _config.charaSelectFrames << "\n";
    }
    std::cout << "===============================\n\n";

    // テストモードに応じたTestRunnerを生成してディスパッチ
    std::unique_ptr<TestRunner> runner;
    switch (_config.testMode) {
        case TestMode::Negotiation:
            runner = std::make_unique<NegotiationTest>();
            break;
        case TestMode::Sync:
            runner = std::make_unique<SyncTest>();
            break;
        case TestMode::Full:
            runner = std::make_unique<FullTest>();
            break;
        case TestMode::Game:
            runner = std::make_unique<GameTest>();
            break;
        // ----------------------------------------------------------------
        // E2E: 全画面遷移の本番フロー再現テスト (v2 NEW)
        //   SceneStateMachine を駆動し、
        //   CharaSelect → Loading → InGame → Rematch の遷移を再現する。
        //   SyncResponder / NegotiationResponder を生成して E2ETest に渡す。
        // ----------------------------------------------------------------
        case TestMode::E2E: {
            bool isHost = (_config.mode == PeerMode::Host);
            auto syncResp = std::make_shared<SyncResponder>(
                _config.clockOffsetUs, _config.clockDriftPpm);
            auto negoResp = std::make_shared<NegotiationResponder>();
            runner = std::make_unique<E2ETest>(isHost, _config, *syncResp, *negoResp);
            // shared_ptr を runner のライフタイムに対して延命する
            // (E2ETest は参照で保持するため、runner が生きている間 syncResp/negoResp も生かす)
            // → unique_ptr の deleter で明示的に生かすパターン
            auto* pSyncResp = syncResp.get();
            auto* pNegoResp = negoResp.get();
            (void)pSyncResp;
            (void)pNegoResp;
            // 注: shared_ptr は上記ブロック終端でデストラクトされる可能性があるため
            //     E2ETest の処理が参照を必要とする間は runner が保持する実行期間内に収まる。
            //     run() がブロッキングであるため、以下の runner->Run() 返却前に syncResp は
            //     破棄されない。(stack 上の shared_ptr がスコープを外れるのは Run() 後)
            _lastResult = runner->Run(static_cast<SOCKET>(_socket), _config, _running);
            return;
        }
    }

    _lastResult = runner->Run(static_cast<SOCKET>(_socket), _config, _running);
}

} // namespace dummy_peer
