#include "FullTest.hpp"
#include "PhaseHandler.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include "RealClock.hpp"
#include <thread>

namespace dummy_peer {

TestResult FullTest::Run(SOCKET sock, const Config& config, std::atomic<bool>& running) {
    bool isHost = (config.mode == PeerMode::Host);
    PhaseHandler handler(isHost);
    handler.SetPhase(Phase::CHARA_SELECT);

    sockaddr_in remoteAddr = MakeRemoteAddr(config.targetIp, config.remotePort);
    auto startTime = std::chrono::steady_clock::now();

    // sendFunc ラムダ
    auto sendFunc = [&](const std::vector<uint8_t>& pkt) {
        sendto(sock,
               reinterpret_cast<const char*>(pkt.data()),
               static_cast<int>(pkt.size()), 0,
               (sockaddr*)&remoteAddr, sizeof(remoteAddr));
    };

    // Phase 1.5: キャラセレ同期ポイント (CS_SYNC_READY + TIME_SYNC)
    std::cout << "[FULL] Phase 1.5: CharaSelect Sync Point (200F)\n";
    while (running && !handler.IsTimeSyncComplete()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        if (elapsed >= config.testDurationSec) {
            std::cout << "[FULL] Timeout in Phase 1.5\n";
            break;
        }

        char recvBuf[1024];
        sockaddr_in fromAddr{};
        int fromLen = sizeof(fromAddr);
        int recvLen = recvfrom(sock, recvBuf, sizeof(recvBuf), 0,
                               (sockaddr*)&fromAddr, &fromLen);
        if (recvLen > 0) {
            handler.HandlePacket(reinterpret_cast<uint8_t*>(recvBuf), recvLen, sendFunc);
        }

        auto pkt = handler.GeneratePacket();
        sendFunc(pkt);

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (!handler.IsTimeSyncComplete()) {
        std::cout << "[FULL] Phase 1.5 failed: time sync incomplete\n";
        goto report;
    }

    // Phase 2: キャラセレ入力交換 (3秒間)
    std::cout << "[FULL] Phase 2: CharaSelect Input Exchange (3 sec)\n";
    {
        auto phase2Start = std::chrono::steady_clock::now();
        while (running) {
            auto now = std::chrono::steady_clock::now();
            auto phase2Elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - phase2Start).count();
            if (phase2Elapsed >= 3) break;

            auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
            if (totalElapsed >= config.testDurationSec) break;

            char recvBuf[1024];
            sockaddr_in fromAddr{};
            int fromLen = sizeof(fromAddr);
            int recvLen = recvfrom(sock, recvBuf, sizeof(recvBuf), 0,
                                   (sockaddr*)&fromAddr, &fromLen);
            if (recvLen > 0) {
                handler.HandlePacket(reinterpret_cast<uint8_t*>(recvBuf), recvLen, sendFunc);
            }

            auto pkt = handler.GeneratePacket();
            sendFunc(pkt);

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    // Phase 4: 対戦前同期 (RNG_SYNC + READY)
    std::cout << "[FULL] Phase 4: Pre-Game Sync (RNG + READY)\n";
    handler.SetPhase(Phase::PRE_GAME_SYNC);
    {
        auto phase4Start = std::chrono::steady_clock::now();
        while (running && !(handler.IsRngSynced() && handler.IsOpponentReady())) {
            auto now = std::chrono::steady_clock::now();
            auto phase4Elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - phase4Start).count();
            if (phase4Elapsed >= 5) {
                std::cout << "[FULL] Phase 4 timeout\n";
                break;
            }

            char recvBuf[1024];
            sockaddr_in fromAddr{};
            int fromLen = sizeof(fromAddr);
            int recvLen = recvfrom(sock, recvBuf, sizeof(recvBuf), 0,
                                   (sockaddr*)&fromAddr, &fromLen);
            if (recvLen > 0) {
                handler.HandlePacket(reinterpret_cast<uint8_t*>(recvBuf), recvLen, sendFunc);
            }

            auto pkt = handler.GeneratePacket();
            sendFunc(pkt);

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

report:
    std::cout << "\n=== DummyPeer [" << (isHost ? "HOST" : "CLIENT")
              << "] Full Test Report ===\n";
    handler.PrintReport();
    bool success = handler.IsTimeSyncComplete() && handler.IsRngSynced() && handler.IsOpponentReady();
    std::cout << "    Full Test:      " << (success ? "SUCCESS" : "INCOMPLETE") << "\n";
    std::cout << "=============================\n";

    TestResult result;
    result.totalFrames = 0;
    result.packetsSent = handler.GetPacketsSent();
    result.syncCompleted = success;
    return result;
}

} // namespace dummy_peer
