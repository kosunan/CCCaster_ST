#include "SyncTest.hpp"
#include "NetworkSimulator.hpp"
#include "InputGenerator.hpp"
#include "SyncResponder.hpp"
#include "TestDiagnostics.hpp"
#include "NegotiationTest.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <cstring>
#include "RealClock.hpp"
#include <thread>
#include <random>

namespace dummy_peer {

TestResult SyncTest::Run(SOCKET sock, const Config& config, std::atomic<bool>& running) {
    NetworkSimulator netSim(
        config.minDelayUs, config.maxDelayUs,
        config.packetLossRate,
        config.spikeDelayUs, config.spikeChance
    );
    InputGenerator inputGen;
    SyncResponder syncResp(config.clockOffsetUs, config.clockDriftPpm);

    if (config.mode == PeerMode::Client) {
        std::cout << "[SyncTest] Pre-running NegotiationTest...\n";
        NegotiationTest negoTest;
        auto negoRes = negoTest.Run(sock, config, running);
        if (!negoRes.syncCompleted) {
            std::cout << "[SyncTest] Negotiation failed. Aborting sync test.\n";
            TestResult res{};
            res.syncCompleted = false;
            return res;
        }
        std::cout << "[SyncTest] Negotiation OK! Starting clock sync pings.\n";
    }

    auto startTime = std::chrono::steady_clock::now();
    uint32_t frameCount = 0;

    // ランダムゲームスピード
    std::mt19937 speedRng(std::random_device{}());
    std::uniform_int_distribution<int> frameTimeDist(
        config.minFrameTimeMs, config.maxFrameTimeMs);
    std::uniform_int_distribution<int> burstTimeDist(1, 3);
    std::uniform_real_distribution<double> burstChanceDist(0.0, 1.0);
    int burstRemaining = 0;
    uint64_t totalBurstFrames = 0;

    // 診断ツール
    FrameSequenceValidator frameValidator;
    FrameIntervalHistogram intervalHist;
    RollbackSimulator rollbackSim(config.rollupSpeed);
    auto lastFrameTime = std::chrono::steady_clock::now();

    // 同期用変数 (Clientモードのみ使用)
    int syncPingCount = 0;
    const int SYNC_PING_MAX = 30;
    int64_t syncIntervalUs = 200000; // 200ms間隔
    int64_t lastSyncPingUs = 0;
    int64_t bestRttUs = INT64_MAX;
    int64_t bestOffsetUs = 0;
    bool syncDone = false;

    sockaddr_in remoteAddr = MakeRemoteAddr(config.targetIp, config.remotePort);

    while (running) {
        // テスト時間チェック
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= config.testDurationSec) {
            break;
        }

        auto nowUs = GetNowUs();

        // --- 1. 受信処理 ---
        char recvBuf[1024];
        sockaddr_in fromAddr{};
        int fromLen = sizeof(fromAddr);
        int recvLen = recvfrom(sock, recvBuf, sizeof(recvBuf), 0,
                               (sockaddr*)&fromAddr, &fromLen);

        if (recvLen > 0) {
            std::vector<uint8_t> recvData(recvBuf, recvBuf + recvLen);
            uint8_t pktType = recvData[0];

            if (config.mode == PeerMode::Host && pktType == SyncResponder::SYNC_REQ) {
                auto response = syncResp.HandlePacket(recvData);
                if (!response.empty()) {
                    netSim.Enqueue(response, config.targetIp, config.remotePort);
                }
            } else if (config.mode == PeerMode::Client && pktType == SyncResponder::SYNC_RES && recvData.size() >= 25) {
                int64_t t1 = 0, t2 = 0, t3 = 0;
                std::memcpy(&t1, recvData.data() + 1, 8);
                std::memcpy(&t2, recvData.data() + 9, 8);
                std::memcpy(&t3, recvData.data() + 17, 8);
                int64_t t4 = nowUs;

                int64_t rtt = (t4 - t1) - (t3 - t2);
                int64_t offset = ((t2 - t1) + (t3 - t4)) / 2;

                if (rtt < bestRttUs) {
                    bestRttUs = rtt;
                    bestOffsetUs = offset;
                }
                std::cout << "  [SYNC] Ping #" << syncPingCount 
                          << " RTT=" << rtt << "us, θ=" << offset << "us"
                          << (rtt == bestRttUs ? " *BEST*" : "") << "\n";
            } else if (pktType == 0x20 && recvData.size() >= 7) {
                uint32_t remoteFrame = 0;
                std::memcpy(&remoteFrame, recvData.data() + 1, 4);
                frameValidator.Validate(remoteFrame);
                rollbackSim.OnRemoteInput(frameCount, remoteFrame);
            }
        }

        // --- 2. 遅延キューから送信可能パケットを放出 ---
        auto readyPackets = netSim.PopReady(nowUs);
        for (auto& pkt : readyPackets) {
            sendto(sock, 
                   reinterpret_cast<const char*>(pkt.data.data()), 
                   static_cast<int>(pkt.data.size()), 0,
                   (sockaddr*)&remoteAddr, sizeof(remoteAddr));
        }

        // --- 3. 毎フレームのランダム入力パケット送信 ---
        {
            uint16_t input = inputGen.Generate();
            std::vector<uint8_t> inputPkt(7);
            inputPkt[0] = 0x20;
            std::memcpy(inputPkt.data() + 1, &frameCount, 4);
            std::memcpy(inputPkt.data() + 5, &input, 2);
            netSim.Enqueue(inputPkt, config.targetIp, config.remotePort);
        }

        // --- 3.5 Clientモード: SYNC_REQの能動送信 ---
        if (config.mode == PeerMode::Client && !syncDone && syncPingCount < SYNC_PING_MAX) {
            if (nowUs - lastSyncPingUs >= syncIntervalUs) {
                std::vector<uint8_t> syncReq(9);
                syncReq[0] = SyncResponder::SYNC_REQ;
                std::memcpy(syncReq.data() + 1, &nowUs, 8);
                netSim.Enqueue(syncReq, config.targetIp, config.remotePort);
                lastSyncPingUs = nowUs;
                syncPingCount++;

                if (syncPingCount >= SYNC_PING_MAX) {
                    syncDone = true;
                    std::cout << "\n  [SYNC DONE] Best RTT=" << bestRttUs 
                              << "us, Final θ=" << bestOffsetUs << "us\n\n";
                }
            }
        }

        frameCount++;

        // ロールバック/ロールアップ シミュレーション
        rollbackSim.Tick();

        // フレーム間隔計測
        auto currentFrameTime = std::chrono::steady_clock::now();
        int intervalMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            currentFrameTime - lastFrameTime).count());
        intervalHist.Record(intervalMs);
        lastFrameTime = currentFrameTime;

        // --- 4. ゲームスピード制御（バーストモード対応） ---
        int frameTimeMs;
        if (burstRemaining > 0) {
            frameTimeMs = burstTimeDist(speedRng);
            burstRemaining--;
            totalBurstFrames++;
        } else {
            if (burstChanceDist(speedRng) < 0.15) {
                burstRemaining = 2;
                frameTimeMs = burstTimeDist(speedRng);
                totalBurstFrames++;
            } else {
                frameTimeMs = frameTimeDist(speedRng);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(frameTimeMs));
    }

    // --- 終了レポート ---
    double avgFps = (frameCount > 0) ? 
        (frameCount / std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count()) : 0;

    std::cout << "\n=== DummyPeer [" << (config.mode == PeerMode::Host ? "HOST" : "CLIENT") << "] Test Report ===\n";
    std::cout << "  Total Frames:  " << frameCount << "\n";
    std::cout << "  Avg FPS:       " << avgFps << "\n";
    std::cout << "  Burst Frames:  " << totalBurstFrames << " (" 
              << (frameCount > 0 ? (100.0 * totalBurstFrames / frameCount) : 0) << "%)\n";
    std::cout << "  Packets Sent:  " << netSim.totalSent << "\n";
    std::cout << "  Packets Lost:  " << netSim.totalDropped << "\n";
    std::cout << "  Spike Events:  " << netSim.totalSpiked << "\n";
    std::cout << "  Effective Loss Rate: " 
              << (netSim.totalSent > 0 ? (100.0 * netSim.totalDropped / (netSim.totalSent + netSim.totalDropped)) : 0)
              << "%\n";
    if (config.mode == PeerMode::Client) {
        std::cout << "  --- Sync Result ---\n";
        std::cout << "    Best RTT:    " << bestRttUs << " us\n";
        std::cout << "    Final θ:     " << bestOffsetUs << " us\n";
        std::cout << "    Sync Pings:  " << syncPingCount << "/" << SYNC_PING_MAX << "\n";
    }
    frameValidator.PrintReport();
    intervalHist.PrintReport();
    rollbackSim.PrintReport();
    std::cout << "=============================\n";

    TestResult result;
    result.totalFrames = frameCount;
    result.avgFps = avgFps;
    result.totalBurstFrames = totalBurstFrames;
    result.packetsSent = netSim.totalSent;
    result.packetsDropped = netSim.totalDropped;
    result.effectiveLossRate = (netSim.totalSent > 0) ?
        (static_cast<double>(netSim.totalDropped) / (netSim.totalSent + netSim.totalDropped)) : 0;
    result.frameGaps = frameValidator.GetGaps();
    result.frameOutOfOrder = frameValidator.GetOutOfOrder();
    result.syncCompleted = syncDone;
    result.estimatedOffsetUs = bestOffsetUs;
    result.estimatedRttUs = bestRttUs;
    return result;
}

} // namespace dummy_peer
