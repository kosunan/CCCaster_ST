#include "NegotiationTest.hpp"
#include "NegotiationResponder.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include "RealClock.hpp"
#include <thread>

namespace dummy_peer {

TestResult NegotiationTest::Run(SOCKET sock, const Config& config, std::atomic<bool>& running) {
    NegotiationResponder negotiator;

    sockaddr_in remoteAddr = MakeRemoteAddr(config.targetIp, config.remotePort);
    auto startTime = std::chrono::steady_clock::now();
    auto connectedTime = std::chrono::steady_clock::time_point::min();
    bool lockedIn = false;

    std::cout << "[NEGOTIATION] Waiting for connection...\n";

    while (running) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        if (elapsed >= config.testDurationSec) {
            std::cout << "[NEGOTIATION] Timeout reached (" << config.testDurationSec << "s).\n";
            break;
        }

        // 受信処理
        char recvBuf[256];
        sockaddr_in fromAddr{};
        int fromLen = sizeof(fromAddr);
        int recvLen = recvfrom(sock, recvBuf, sizeof(recvBuf), 0,
                               (sockaddr*)&fromAddr, &fromLen);

        if (recvLen >= NegotiationResponder::PACKET_SIZE) {
            auto response = negotiator.HandlePacket(
                reinterpret_cast<uint8_t*>(recvBuf), recvLen);

            if (!response.empty()) {
                sockaddr_in* target = (config.mode == PeerMode::Host) ? &fromAddr : &remoteAddr;
                sendto(sock,
                       reinterpret_cast<const char*>(response.data()),
                       static_cast<int>(response.size()), 0,
                       (sockaddr*)target, sizeof(*target));
            }

            if (!lockedIn && negotiator.IsConnected()) {
                if (connectedTime == std::chrono::steady_clock::time_point::min()) {
                    connectedTime = now;
                    char clientIp[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &fromAddr.sin_addr, clientIp, sizeof(clientIp));
                    std::cout << "[NEGOTIATION] Connection established with "
                              << clientIp << ":" << ntohs(fromAddr.sin_port) << "\n";
                }
            }
        }

        if (!lockedIn && negotiator.IsConnected() && connectedTime != std::chrono::steady_clock::time_point::min()) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - connectedTime).count() >= 2) {
                lockedIn = true;
                std::cout << "[NEGOTIATION] Auto Lock-in! Connection ready.\n";
                break;
            }
        }

        // Client は能動的にパケットを送信する (connected前から送信開始)
        if (config.mode == PeerMode::Client || negotiator.IsConnected()) {
            auto pkt = negotiator.CreatePacket();
            sendto(sock,
                   reinterpret_cast<const char*>(pkt.data()),
                   static_cast<int>(pkt.size()), 0,
                   (sockaddr*)&remoteAddr, sizeof(remoteAddr));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // レポート出力
    std::cout << "\n=== DummyPeer [" << (config.mode == PeerMode::Host ? "HOST" : "CLIENT")
              << "] Negotiation Report ===\n";
    negotiator.PrintReport();
    std::cout << "    Lock-in:        " << (lockedIn ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "=============================\n";

    TestResult result;
    result.totalFrames = 0;
    result.packetsSent = negotiator.GetPacketsSent();
    result.syncCompleted = lockedIn;
    return result;
}

} // namespace dummy_peer
