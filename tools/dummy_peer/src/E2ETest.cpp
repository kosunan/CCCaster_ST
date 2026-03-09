#include "E2ETest.hpp"
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include "RealClock.hpp"
#include <thread>

namespace dummy_peer {

E2ETest::E2ETest(bool isHost, const Config& config,
                  SyncResponder& syncResp,
                  NegotiationResponder& negoResp)
    : _isHost(isHost), _config(config),
      _syncResp(syncResp), _negoResp(negoResp)
{}

TestResult E2ETest::Run(SOCKET sock, const Config& config,
                         std::atomic<bool>& running) {
    TestResult result;
    result.syncCompleted = false;

    sockaddr_in remoteAddr{};
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port   = htons(config.remotePort);
    remoteAddr.sin_addr.s_addr = inet_addr(config.targetIp.c_str());

    // --- 送信ラムダ ---
    auto sendFunc = [&](const std::vector<uint8_t>& pkt) {
        sendto(sock,
               reinterpret_cast<const char*>(pkt.data()),
               static_cast<int>(pkt.size()),
               0,
               reinterpret_cast<const sockaddr*>(&remoteAddr),
               sizeof(remoteAddr));
        result.packetsSent++;
    };

    // --- SceneStateMachine 初期化 ---
    SceneStateMachine sm(config, _isHost, _syncResp);

    std::cout << "[E2ETest] Start. isHost=" << _isHost << "\n";

    auto testStart = std::chrono::steady_clock::now();
    constexpr int FRAME_MS = 16;

    // === メインループ ===
    while (running.load()) {
        auto frameStart = std::chrono::steady_clock::now();

        // タイムアウト判定
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            frameStart - testStart).count();
        if (elapsed >= config.testDurationSec) {
            std::cout << "[E2ETest] Duration expired.\n";
            break;
        }

        // --- 受信処理 ---
        uint8_t recvBuf[4096];
        sockaddr_in fromAddr{};
        int fromLen = sizeof(fromAddr);
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv{0, 0};
        while (select(0, &fds, nullptr, nullptr, &tv) > 0 &&
               FD_ISSET(sock, &fds)) {
            int rcvd = recvfrom(sock,
                                reinterpret_cast<char*>(recvBuf),
                                sizeof(recvBuf),
                                0,
                                reinterpret_cast<sockaddr*>(&fromAddr),
                                &fromLen);
            if (rcvd > 0) {
                result.totalFrames++;
                sm.HandlePacket(recvBuf, rcvd, sendFunc);
            }
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            tv = {0, 0};
        }

        // --- 送信処理(毎フレーム) ---
        sm.Update(sendFunc);

        // --- 完了判定 ---
        if (sm.IsDone()) {
            std::cout << "[E2ETest] SceneStateMachine done!\n";
            result.syncCompleted = true;
            break;
        }

        // --- 16ms スリープ ---
        auto frameEnd = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            frameEnd - frameStart).count();
        if (elapsed_ms < FRAME_MS) {
            Sleep(static_cast<DWORD>(FRAME_MS - elapsed_ms));
        }
    }

    sm.PrintReport();
    std::cout << "[E2ETest] Done. syncCompleted=" << result.syncCompleted
              << " packets=" << result.packetsSent << "\n";
    return result;
}

} // namespace dummy_peer
