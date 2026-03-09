#include "GameTest.hpp"
#include "NegotiationResponder.hpp"
#include "SyncResponder.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <cstring>
#include "RealClock.hpp"
#include <cstdlib>
#include <ctime>

// ================================================================
// HeldInputGenerator — ボタンをホールドしてリアルな入力を模擬
//
// 【設計】
//   同じ入力値を holdFrames フレーム間維持してから新しい入力に切り替える。
//   ゲームが入力を1F以上読み込まなければアクションは発生しないため、
//   毎パケット完全ランダムだとボタンが即リリースされてしまう問題を解決。
//
// 【出力フォーマット】
//   uint16_t = ボタンビットマスク (MBAA CC_BUTTON_* 互換)
//   ※ 方向キーは含まない（DLL側が direction=0 として処理）
// ================================================================
struct HeldInputGenerator {
    // MBAA ボタン定数 (CC_BUTTON_* と同じ値)
    static constexpr uint16_t BTN_A       = 0x0001;
    static constexpr uint16_t BTN_B       = 0x0002;
    static constexpr uint16_t BTN_C       = 0x0004;
    static constexpr uint16_t BTN_D       = 0x0008;
    static constexpr uint16_t BTN_E       = 0x0010;
    static constexpr uint16_t BTN_AB      = 0x0020;
    static constexpr uint16_t BTN_START   = 0x0040;
    static constexpr uint16_t BTN_FN1     = 0x0080;
    static constexpr uint16_t BTN_FN2     = 0x0100;
    static constexpr uint16_t BTN_CONFIRM = 0x0200;
    static constexpr uint16_t BTN_CANCEL  = 0x0400;

    // 毎パケットごとにランダムなボタン1つ（またはニュートラル）を返す
    uint16_t Next() {
        static const uint16_t patterns[] = {
            0,                             // ニュートラル
            BTN_A | BTN_CONFIRM,           // A
            BTN_B | BTN_CANCEL,            // B
            BTN_C,                         // C
            BTN_D,                         // D
            BTN_E,                         // E
            BTN_AB,                        // A+B マクロ
            BTN_START,                     // Start
            BTN_FN1,                       // FN1
            BTN_FN2,                       // FN2
            0,                             // ニュートラル
        };
        return patterns[rand() % (sizeof(patterns) / sizeof(patterns[0]))];
    }
};

namespace dummy_peer {

// ================================================================
// RecvAndHandleSync — パケット受信しつつSYNC応答を維持するヘルパー
//
// 【3バイトパケット対応 (旧互換フォーマット)】
//   DLL側 PacketRouter が旧形式で送信する場合:
//     byte[0] = type, byte[1..2] = uint16_t ペイロード
//     0x20 = CS_INPUT      (キャラセレ入力)
//     0x21 = LOADING_INPUT (ロード画面入力)
//     0x22 = REMATCH_MENU  (リマッチ選択)
//   これらを受信してログ記録・統計カウントし、呼び出し元に len を返す。
//
// 戻り値: -1=SYNCパケット処理済み, 0=受信なし, >0=通常パケットのサイズ
// ================================================================
int GameTest::RecvAndHandleSync(SOCKET sock, sockaddr_in& confirmedRemoteAddr,
                                SyncResponder& syncResp) {
    char buf[256];
    sockaddr_in from{};
    int fromLen = sizeof(from);
    int len = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
    if (len > 0) {
        uint8_t type = static_cast<uint8_t>(buf[0]);
        if (type == SyncResponder::SYNC_REQ && len >= 9) {
            std::vector<uint8_t> data(buf, buf + len);
            auto resp = syncResp.HandlePacket(data);
            if (!resp.empty()) {
                sendto(sock,
                       reinterpret_cast<const char*>(resp.data()),
                       static_cast<int>(resp.size()), 0,
                       (sockaddr*)&confirmedRemoteAddr, sizeof(confirmedRemoteAddr));
            }
            return -1;
        }
        // SYNC_DONEも消費して記録
        if (type == SyncResponder::SYNC_DONE && len >= 9) {
            _lastSyncDoneRecvTime = std::chrono::steady_clock::now();
            _syncDoneRecvCount++;
            int64_t peerStart = 0;
            memcpy(&peerStart, buf + 1, 8);
            // DLLのSYNC_DONEを受けたら、こちらもSYNC_DONEを送る（まだ送っていなければ）
            if (!_mySyncDoneSent) {
                int64_t startTimeUs = syncResp.GetLocalTimeUs() + 200000;
                std::vector<uint8_t> done(9);
                done[0] = SyncResponder::SYNC_DONE;
                memcpy(done.data() + 1, &startTimeUs, 8);
                sendto(sock,
                       reinterpret_cast<const char*>(done.data()),
                       static_cast<int>(done.size()), 0,
                       (sockaddr*)&confirmedRemoteAddr, sizeof(confirmedRemoteAddr));
                _mySyncDoneSent = true;
                _syncDoneSentCount++;
                std::cout << "[GAME] SYNC_DONE #" << _syncDoneSentCount << " sent (reactive)\n";
            }
            // 開始時刻同期
            int64_t localStart = syncResp.ConvertRemoteToLocal(peerStart);
            int64_t nowUs = syncResp.GetLocalTimeUs();
            int64_t waitUs = localStart - nowUs;
            if (waitUs > 0 && waitUs < 2000000) {
                while (syncResp.GetLocalTimeUs() < localStart) { /* spin */ }
            }
            std::cout << "[GAME] DLL sync point (SYNC_DONE #" << _syncDoneRecvCount
                      << " recv, wait=" << waitUs << "us)\n";
            _mySyncDoneSent = false; // 次の同期ポイントに備えてリセット
            return -1;
        }
        // SYNC_RES も処理（DummyPeer側がSYNC_REQを送った場合の戻り）
        if (type == SyncResponder::SYNC_RES && len >= 25) {
            int64_t t1 = 0, t2 = 0, t3 = 0;
            memcpy(&t1, buf + 1, 8);
            memcpy(&t2, buf + 9, 8);
            memcpy(&t3, buf + 17, 8);
            int64_t t4 = syncResp.GetLocalTimeUs();
            int64_t rtt = (t4 - t1) - (t3 - t2);
            int64_t offset = ((t2 - t1) + (t3 - t4)) / 2;
            if (rtt >= 0 && rtt < _bestRtt) {
                _bestRtt = rtt;
                _bestOffset = offset;
                syncResp.SetClockOffset(offset);
            }
            _syncSamplesRecv++;
            return -1;
        }

        // ──────────────────────────────────────────────────────────────
        // 旧形式 3Bシーン同期パケット
        //   byte[0] = type, byte[1..2] = uint16_t ペイロード
        //   DLL側 PacketRouter の後方互換フォーマット
        //
        //   0x20 = CS_INPUT      : キャラセレ入力
        //   0x21 = LOADING_INPUT : ロード画面入力
        //   0x22 = REMATCH_MENU  : リマッチ選択 (byte[1] = int8_t menuIndex)
        //
        // DummyPeer としてはこれらを受け取ってログ記録するだけでよい。
        // 応答パケットの生成（CS_INPUTの折り返し等）が必要になった場合は
        // 各 DummyScene を実装した際に拡張すること。
        // ──────────────────────────────────────────────────────────────
        if (len == 3 && (type == 0x20 || type == 0x21 || type == 0x22)) {
            uint16_t payload = 0;
            memcpy(&payload, buf + 1, 2);

            switch (type) {
            case 0x20: // CS_INPUT
                _legacy3bCsInputRecv++;
                std::cout << "[GAME] LEGACY CS_INPUT recv: val=0x"
                          << std::hex << payload << std::dec
                          << " (total=" << _legacy3bCsInputRecv << ")\n";
                break;
            case 0x21: // LOADING_INPUT
                _legacy3bLoadingRecv++;
                std::cout << "[GAME] LEGACY LOADING_INPUT recv: val=0x"
                          << std::hex << payload << std::dec
                          << " (total=" << _legacy3bLoadingRecv << ")\n";
                break;
            case 0x22: // REMATCH_MENU
                _legacy3bRematchRecv++;
                std::cout << "[GAME] LEGACY REMATCH_MENU recv: menuIndex="
                          << static_cast<int>(static_cast<int8_t>(payload & 0xFF))
                          << " (total=" << _legacy3bRematchRecv << ")\n";
                break;
            }
            return len; // 呼び出し元で inputPacketsRecv としてカウント
        }

        return len;
    }
    return 0;
}

// ================================================================
// RunInitialSync — 最初の同期フェーズ（Negotiation後に1回だけ呼ぶ）
// DLLがTimeSynchronizer.Update()でSYNC_REQを送り始めるのを待つスタイル
// ================================================================
bool GameTest::RunInitialSync(SOCKET sock, const Config& config, std::atomic<bool>& running,
                               sockaddr_in& confirmedRemoteAddr,
                               SyncResponder& syncResp,
                               std::chrono::steady_clock::time_point endTime,
                               HeldInputGenerator& inputGen) {
    constexpr int SYNC_PINGS = 10;
    constexpr int64_t PING_INTERVAL_US = 50000; // 50ms
    int pingsSent = 0;
    int64_t lastPingUs = 0;
    bool myDoneSent = false;
    bool peerDoneRecv = false;
    int64_t peerStartTimeUs = 0;
    int sampleCount = 0;
    int64_t lastInputSendUs = syncResp.GetRealQpcTimeUs();
    uint32_t inputSentInSync = 0;

    std::cout << "[GAME] Initial sync: exchanging SYNC_REQ/RES...\n";

    while (running.load() && !myDoneSent && (config.testDurationSec == 0 || std::chrono::steady_clock::now() < endTime)) {
        char buf[256];
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
        if (len > 0) {
            uint8_t type = static_cast<uint8_t>(buf[0]);
            if (type == SyncResponder::SYNC_REQ && len >= 9) {
                std::vector<uint8_t> data(buf, buf + len);
                auto resp = syncResp.HandlePacket(data);
                if (!resp.empty()) {
                    sendto(sock, reinterpret_cast<const char*>(resp.data()),
                           static_cast<int>(resp.size()), 0,
                           (sockaddr*)&confirmedRemoteAddr, sizeof(confirmedRemoteAddr));
                }
            } else if (type == SyncResponder::SYNC_RES && len >= 25) {
                int64_t t1 = 0, t2 = 0, t3 = 0;
                memcpy(&t1, buf + 1, 8);
                memcpy(&t2, buf + 9, 8);
                memcpy(&t3, buf + 17, 8);
                int64_t t4 = syncResp.GetLocalTimeUs();
                int64_t rtt = (t4 - t1) - (t3 - t2);
                int64_t offset = ((t2 - t1) + (t3 - t4)) / 2;
                if (rtt >= 0 && rtt < _bestRtt) {
                    _bestRtt = rtt;
                    _bestOffset = offset;
                }
                sampleCount++;
            } else if (type == SyncResponder::SYNC_DONE && len >= 9) {
                memcpy(&peerStartTimeUs, buf + 1, 8);
                peerDoneRecv = true;
            }
        }

        // DummyPeerもSYNC_REQを送信
        if (pingsSent < SYNC_PINGS) {
            int64_t nowUs = syncResp.GetLocalTimeUs();
            if (nowUs - lastPingUs >= PING_INTERVAL_US) {
                lastPingUs = nowUs;
                pingsSent++;
                std::vector<uint8_t> req(9);
                req[0] = SyncResponder::SYNC_REQ;
                memcpy(req.data() + 1, &nowUs, 8);
                sendto(sock, reinterpret_cast<const char*>(req.data()),
                       static_cast<int>(req.size()), 0,
                       (sockaddr*)&confirmedRemoteAddr, sizeof(confirmedRemoteAddr));
            }
        } else if (!myDoneSent) {
            syncResp.SetClockOffset(_bestOffset);
            std::cout << "[GAME] Initial sync: θ=" << _bestOffset
                      << "us RTT=" << _bestRtt << "us (samples=" << sampleCount << ")\n";
            Sleep(100);
            int64_t startTimeUs = syncResp.GetLocalTimeUs() + 200000;
            std::vector<uint8_t> done(9);
            done[0] = SyncResponder::SYNC_DONE;
            memcpy(done.data() + 1, &startTimeUs, 8);
            sendto(sock, reinterpret_cast<const char*>(done.data()),
                   static_cast<int>(done.size()), 0,
                   (sockaddr*)&confirmedRemoteAddr, sizeof(confirmedRemoteAddr));
            myDoneSent = true;
            _syncDoneSentCount++;
        }

        // SYNC交換中もコントローラ入力を60fpsで送信し続ける
        {
            int64_t nowInputUs = syncResp.GetRealQpcTimeUs();
            if (nowInputUs - lastInputSendUs >= 16666) {
                uint16_t randomInput = inputGen.Next();
                sendto(sock, reinterpret_cast<const char*>(&randomInput),
                       sizeof(randomInput), 0,
                       (sockaddr*)&confirmedRemoteAddr, sizeof(confirmedRemoteAddr));
                lastInputSendUs = nowInputUs;
                inputSentInSync++;
            }
        }
        Sleep(5);
    }

    // DLLのSYNC_DONE待ち
    if (!peerDoneRecv) {
        std::cout << "[GAME] Waiting for DLL's SYNC_DONE (20s timeout)...\n";
        auto waitEnd = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (running.load() && !peerDoneRecv && std::chrono::steady_clock::now() < waitEnd) {
            char buf[256];
            sockaddr_in from{};
            int fromLen = sizeof(from);
            int len = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
            if (len > 0) {
                uint8_t type = static_cast<uint8_t>(buf[0]);
                if (type == SyncResponder::SYNC_DONE && len >= 9) {
                    memcpy(&peerStartTimeUs, buf + 1, 8);
                    peerDoneRecv = true;
                } else if (type == SyncResponder::SYNC_REQ && len >= 9) {
                    std::vector<uint8_t> data(buf, buf + len);
                    auto resp = syncResp.HandlePacket(data);
                    if (!resp.empty()) {
                        sendto(sock, reinterpret_cast<const char*>(resp.data()),
                               static_cast<int>(resp.size()), 0,
                               (sockaddr*)&confirmedRemoteAddr, sizeof(confirmedRemoteAddr));
                    }
                }
            }

            // SYNC_DONE待ち中もコントローラ入力を60fpsで送信し続ける
            {
                int64_t nowInputUs = syncResp.GetRealQpcTimeUs();
                if (nowInputUs - lastInputSendUs >= 16666) {
                    uint16_t randomInput = inputGen.Next();
                    sendto(sock, reinterpret_cast<const char*>(&randomInput),
                           sizeof(randomInput), 0,
                           (sockaddr*)&confirmedRemoteAddr, sizeof(confirmedRemoteAddr));
                    lastInputSendUs = nowInputUs;
                    inputSentInSync++;
                }
            }
            Sleep(5);
        }
    }

    if (peerDoneRecv) {
        int64_t localStart = syncResp.ConvertRemoteToLocal(peerStartTimeUs);
        int64_t nowUs = syncResp.GetLocalTimeUs();
        int64_t waitUs = localStart - nowUs;
        std::cout << "[GAME] DLL ready! wait=" << waitUs << "us\n";
        if (waitUs > 0 && waitUs < 2000000) {
            while (syncResp.GetLocalTimeUs() < localStart) { /* spin */ }
        }
        std::cout << "[GAME] Initial sync START! (input sent during sync: " << inputSentInSync << ")\n";
    }

    return myDoneSent;
}

// ================================================================
// GameTest::Run — リアクティブループ方式
//
// 設計思想:
//   固定の「同期ポイント1→キャラセレ→同期ポイント2→対戦」ではなく、
//   Negotiation後はひたすら:
//     1. SYNC_REQ が来たら SYNC_RES を返す (TimeSynchronizer互換)
//     2. SYNC_DONE が来たら SYNC_DONE を返す + 開始時刻同期
//     3. 2バイト入力パケットを60fps定周期で送信し続ける
//   DLL側のフェーズ遷移(CharaSelect→Loading→InGame→Rematch→Loop)に
//   DummyPeerは関知しない。DLLが何回再同期しても自動追従する。
// ================================================================
TestResult GameTest::Run(SOCKET sock, const Config& config, std::atomic<bool>& running) {
    NegotiationResponder negotiator;

    sockaddr_in remoteAddr = MakeRemoteAddr(config.targetIp, config.remotePort);
    auto startTime = std::chrono::steady_clock::now();
    // duration=0 → 無制限
    auto endTime = (config.testDurationSec > 0)
        ? startTime + std::chrono::seconds(config.testDurationSec)
        : std::chrono::steady_clock::time_point::max();

    bool connected = false;
    bool lockedIn = false;
    uint32_t negoPacketsSent = 0;
    uint32_t negoPacketsRecv = 0;
    uint32_t inputPacketsSent = 0;
    uint32_t inputPacketsRecv = 0;
    HeldInputGenerator inputGen; // ボタンをホールドするリアルな入力生成器
    sockaddr_in confirmedRemoteAddr = remoteAddr;
    auto connectedTime = std::chrono::steady_clock::time_point::min();

    // Phase 1: Negotiation（変更なし）
    std::cout << "[GAME] Phase 1: Negotiation...\n";
    while (running.load() && !lockedIn && std::chrono::steady_clock::now() < endTime) {
        char recvBuf[256];
        sockaddr_in fromAddr{};
        int fromLen = sizeof(fromAddr);
        int recvLen = recvfrom(sock, recvBuf, sizeof(recvBuf), 0,
                               (sockaddr*)&fromAddr, &fromLen);

        if (recvLen >= NegotiationResponder::PACKET_SIZE) {
            negoPacketsRecv++;
            auto response = negotiator.HandlePacket(
                reinterpret_cast<uint8_t*>(recvBuf), recvLen);
            if (!response.empty()) {
                sockaddr_in* target = (config.mode == PeerMode::Host) ? &fromAddr : &remoteAddr;
                sendto(sock, reinterpret_cast<const char*>(response.data()),
                       static_cast<int>(response.size()), 0,
                       (sockaddr*)target, sizeof(*target));
                negoPacketsSent++;
            }
            if (!connected && negotiator.IsConnected()) {
                connected = true;
                connectedTime = std::chrono::steady_clock::now();
                confirmedRemoteAddr = fromAddr;
                char clientIp[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &fromAddr.sin_addr, clientIp, sizeof(clientIp));
                std::cout << "[GAME] Connected to " << clientIp << ":" << ntohs(fromAddr.sin_port) << "\n";
            }
            if (connected && std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - connectedTime).count() >= 2) {
                lockedIn = true;
                std::cout << "[GAME] Lock-in!\n";
            }
        } else if (recvLen > 0 && connected) {
            lockedIn = true;
            confirmedRemoteAddr = fromAddr;
            std::cout << "[GAME] Short packet from DLL → Lock-in!\n";
        }
        if (config.mode == PeerMode::Client || negotiator.IsConnected()) {
            auto pkt = negotiator.CreatePacket();
            sendto(sock, reinterpret_cast<const char*>(pkt.data()),
                   static_cast<int>(pkt.size()), 0,
                   (sockaddr*)&remoteAddr, sizeof(remoteAddr));
            negoPacketsSent++;
        }
        Sleep(16);
    }

    if (!connected) {
        std::cout << "[GAME] Negotiation timeout.\n";
        TestResult r;
        r.packetsSent = negoPacketsSent;
        return r;
    }

    // Phase 2: Initial Sync (最初のSYNC_REQ/RES/DONE交換)
    SyncResponder syncResp(0, 0.0);
    srand(static_cast<unsigned>(time(nullptr)));
    _syncDoneSentCount = 0;
    _syncDoneRecvCount = 0;
    _syncSamplesRecv = 0;
    _bestRtt = INT64_MAX;
    _bestOffset = 0;
    _mySyncDoneSent = false;

    std::cout << "[GAME] Phase 2: Initial TimeSync...\n";
    RunInitialSync(sock, config, running, confirmedRemoteAddr, syncResp, endTime, inputGen);

    // ================================================================
    // Phase 3: リアクティブメインループ
    //
    // ここから先は固定フェーズではない:
    //   - SYNC_REQ/RES/DONE → RecvAndHandleSync() が全自動処理
    //   - 2バイト入力 → 60fps定周期で送信し続ける
    //   - DLL側が Loading→Reset→再同期→InGame→Rematch→CharaSelect を
    //     何サイクル回しても、DummyPeerはSYNC応答+入力送信を継続
    // ================================================================
    std::cout << "[GAME] Phase 3: Reactive main loop (SYNC respond + input send)\n";
    if (config.testDurationSec == 0) {
        std::cout << "[GAME] Duration: UNLIMITED (Ctrl+C or DLL disconnect to stop)\n";
    }

    int64_t lastSendTimeUs = syncResp.GetRealQpcTimeUs();
    uint32_t roundCount = 0;
    uint32_t lastSyncDoneCount = _syncDoneRecvCount;
    auto lastActivityTime = std::chrono::steady_clock::now();
    constexpr int IDLE_TIMEOUT_SEC = 60; // 60秒パケット無しで自動切断

    while (running.load() && std::chrono::steady_clock::now() < endTime) {
        // --- 受信: SYNC/SYNC_DONE/入力パケット全自動処理 ---
        int len = RecvAndHandleSync(sock, confirmedRemoteAddr, syncResp);
        if (len > 0 && len != -1) {
            inputPacketsRecv++;
            lastActivityTime = std::chrono::steady_clock::now();
        } else if (len == -1) {
            // SYNCパケット処理された
            lastActivityTime = std::chrono::steady_clock::now();
        }

        // 新しいSYNC_DONEを受信 = DLLが新しいラウンドに入った
        if (_syncDoneRecvCount > lastSyncDoneCount) {
            roundCount++;
            std::cout << "[GAME] === Round " << roundCount << " sync complete (SYNC_DONE #"
                      << _syncDoneRecvCount << ") ===\n";
            lastSyncDoneCount = _syncDoneRecvCount;
        }

        // --- 送信: 2バイト入力パケット (60fps) ---
        int64_t nowUs = syncResp.GetRealQpcTimeUs();
        int64_t frameInterval = 16666 + (rand() % 4001);
        int64_t elapsed = nowUs - lastSendTimeUs;

        if (elapsed >= frameInterval) {
            uint16_t randomInput = inputGen.Next();
            sendto(sock, reinterpret_cast<const char*>(&randomInput),
                   sizeof(randomInput), 0,
                   (sockaddr*)&confirmedRemoteAddr, sizeof(confirmedRemoteAddr));
            inputPacketsSent++;
            lastSendTimeUs = nowUs;
        } else {
            int64_t remaining = frameInterval - elapsed;
            if (remaining > 2000) {
                Sleep(static_cast<DWORD>((remaining - 2000) / 1000));
            }
        }

        // --- アイドルタイムアウト (無制限モード時の安全装置) ---
        if (config.testDurationSec == 0) {
            auto idleSec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - lastActivityTime).count();
            if (idleSec >= IDLE_TIMEOUT_SEC) {
                std::cout << "[GAME] Idle timeout (" << IDLE_TIMEOUT_SEC
                          << "s no packets). DLL disconnected?\n";
                break;
            }
        }
    }

    // --- 最終レポート ---
    auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();

    std::cout << "\n=== DummyPeer [" << (config.mode == PeerMode::Host ? "HOST" : "CLIENT")
              << "] Game Test Report ===\n";
    std::cout << "  Negotiation Sent:     " << negoPacketsSent << "\n";
    std::cout << "  Negotiation Recv:     " << negoPacketsRecv << "\n";
    std::cout << "  Connected:            " << (connected ? "Yes" : "No") << "\n";
    std::cout << "  Locked In:            " << (lockedIn ? "Yes" : "No") << "\n";
    std::cout << "  SYNC_DONE Sent:       " << _syncDoneSentCount << "\n";
    std::cout << "  SYNC_DONE Recv:       " << _syncDoneRecvCount << "\n";
    std::cout << "  Sync Samples:         " << _syncSamplesRecv << "\n";
    std::cout << "  Rounds Completed:     " << roundCount << "\n";
    std::cout << "  Best RTT:             " << _bestRtt << " us\n";
    std::cout << "  Best θ:               " << _bestOffset << " us\n";
    std::cout << "  Input Packets Sent:   " << inputPacketsSent << "\n";
    std::cout << "  Input Packets Recv:   " << inputPacketsRecv << "\n";
    std::cout << "  Ping:                 " << negotiator.GetCurrentPingMs() << " ms\n";
    std::cout << "  Total Duration:       " << totalElapsed << " ms\n";
    std::cout << "  Game Test:            " << (connected ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "=============================\n";

    TestResult result;
    result.packetsSent = negoPacketsSent + inputPacketsSent;
    result.syncCompleted = connected && lockedIn;
    result.estimatedOffsetUs = _bestOffset;
    result.estimatedRttUs = _bestRtt;
    return result;
}

} // namespace dummy_peer
