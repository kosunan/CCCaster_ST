// ================================================================
// main.cpp — DummyPeer v2 エントリーポイント
//
// 【v2 変更点】
//   main() → WinMain() に変更し非表示ウィンドウを作成する。
//   理由: WASAPI の CoCreateInstance が COM アパートメント非対応の
//   コンソールアプリでは失敗することがある。
//   Hidden Window を持つ GUI アプリにすることで WASAPI が正常に動作する。
//
// 【後方互換】
//   既存の CLI 引数は __argc/__argv でそのまま引き継ぐ。
//   コンソール出力は AllocConsole / freopen で維持する。
// ================================================================

#include "DummyPeer.hpp"
#include "ReportWriter.hpp"
#include <windows.h>
#include <cstdio>
#include <iostream>
#include <string>
#include <cstring>

// ================================================================
// プリセット定義
// ================================================================
static void ApplyPreset(dummy_peer::Config& config, const std::string& preset) {
    if (preset == "lan") {
        config.minDelayUs = 1000;      // 1ms
        config.maxDelayUs = 3000;      // 3ms
        config.packetLossRate = 0.0;
        config.spikeDelayUs = 0;
        config.spikeChance = 0.0;
        config.clockOffsetUs = 50000;  // 50ms
        config.clockDriftPpm = 5.0;
    } else if (preset == "wifi") {
        config.minDelayUs = 10000;     // 10ms
        config.maxDelayUs = 40000;     // 40ms
        config.packetLossRate = 0.02;
        config.spikeDelayUs = 100000;  // 100ms
        config.spikeChance = 0.05;
        config.clockOffsetUs = 150000;
        config.clockDriftPpm = 20.0;
    } else if (preset == "4g") {
        config.minDelayUs = 30000;     // 30ms
        config.maxDelayUs = 80000;     // 80ms
        config.packetLossRate = 0.05;
        config.spikeDelayUs = 300000;  // 300ms
        config.spikeChance = 0.10;
        config.clockOffsetUs = 200000;
        config.clockDriftPpm = 30.0;
    } else if (preset == "hell") {
        config.minDelayUs = 50000;     // 50ms
        config.maxDelayUs = 200000;    // 200ms
        config.packetLossRate = 0.15;
        config.spikeDelayUs = 500000;  // 500ms
        config.spikeChance = 0.20;
        config.clockOffsetUs = 300000;
        config.clockDriftPpm = 50.0;
    } else {
        std::cerr << "[WARNING] Unknown preset: " << preset << " (ignored)\n";
    }
}

static void PrintUsage() {
    std::cout << "Usage: DummyPeer.exe [options]\n";
    std::cout << "\n  === Basic Options ===\n";
    std::cout << "  --mode <host|client>   Peer mode (default: host)\n";
    std::cout << "  --test-mode <mode>     Test mode: negotiation, sync, full, game, e2e (default: sync)\n";
    std::cout << "  --target-ip <ip>       Target IP (default: 127.0.0.1)\n";
    std::cout << "  --local-port <port>    Local bind port (default: 10800)\n";
    std::cout << "  --remote-port <port>   Remote target port (default: 10801)\n";
    std::cout << "  --duration <sec>       Test duration in seconds (default: 10)\n";
    std::cout << "\n  === Network Simulation ===\n";
    std::cout << "  --preset <name>        Apply network preset: lan, wifi, 4g, hell\n";
    std::cout << "  --min-delay <ms>       Minimum delay in ms (default: 30)\n";
    std::cout << "  --max-delay <ms>       Maximum delay in ms (default: 60)\n";
    std::cout << "  --loss <rate>          Packet loss rate 0.0~1.0 (default: 0.0)\n";
    std::cout << "  --spike-delay <ms>     Spike delay in ms (default: 0, disabled)\n";
    std::cout << "  --spike-chance <rate>  Spike chance 0.0~1.0 (default: 0.0)\n";
    std::cout << "\n  === Clock Simulation ===\n";
    std::cout << "  --clock-offset <us>    Clock offset in us (default: 150000)\n";
    std::cout << "  --drift-ppm <ppm>      Clock drift in ppm (default: 20)\n";
    std::cout << "\n  === Game Simulation ===\n";
    std::cout << "  --min-frame-time <ms>  Min frame time in ms (default: 16)\n";
    std::cout << "  --max-frame-time <ms>  Max frame time in ms (default: 30)\n";
    std::cout << "  --rollup-speed <n>     Rollup speed: frames per real frame (default: 5)\n";
    std::cout << "\n  === E2E Test Options ===\n";
    std::cout << "  --round-frames <n>     Frames per round (default: 3600=60s)\n";
    std::cout << "  --max-rounds <n>       Number of rounds (default: 2)\n";
    std::cout << "  --rematch-choice <n>   0=Rematch, 1=CharaSelect (default: 0)\n";
    std::cout << "  --auto-rematch         Enable auto-rematch loop\n";
    std::cout << "  --chara-select-frames <n>  Frames for chara select (default: 300)\n";
    std::cout << "\n  === Disconnect/Reconnect ===\n";
    std::cout << "  --disconnect-at <sec>  Disconnect at specified second (default: disabled)\n";
    std::cout << "  --reconnect-after <s>  Reconnect after specified seconds (default: disabled)\n";
    std::cout << "\n  === Output & Assertions ===\n";
    std::cout << "  --output-json <file>   Write JSON report to file\n";
    std::cout << "  --assert-max-loss <r>  Assert: loss rate must be <= r (0.0~1.0)\n";
    std::cout << "  --assert-no-gaps       Assert: no frame gaps allowed\n";
    std::cout << "  --help                 Show this help\n";
}

// ================================================================
// WinMain — エントリーポイント (v2: main→WinMain変更)
// ================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // コンソール出力の維持（非表示ウィンドウGUIアプリでもstdout出力する）
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        AllocConsole();
    }
    freopen("dummy_out.log", "w", stdout);
    freopen("dummy_err.log", "w", stderr);

    // Hidden Window 作成（WASAPI/COM の安定動作用）
    WNDCLASSA wc = {};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "DummyPeerHiddenWindow";
    RegisterClassA(&wc);
    CreateWindowA(wc.lpszClassName, "", 0, 0, 0, 0, 0,
                 HWND_MESSAGE, NULL, hInstance, NULL);

    // CLI 引数 (__argc / __argv は WIN32 でCRT経由で取得可能)
    int argc = __argc;
    char** argv = __argv;

    dummy_peer::Config config;
    bool modeSet = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help") { PrintUsage(); return 0; }
        if (arg == "--mode" && i+1 < argc) {
            std::string m = argv[++i];
            if (m == "host") {
                config.mode = dummy_peer::PeerMode::Host;
                if (!modeSet) { config.localPort = 10800; config.remotePort = 10801; }
            } else if (m == "client") {
                config.mode = dummy_peer::PeerMode::Client;
                if (!modeSet) { config.localPort = 10801; config.remotePort = 10800; }
            }
            modeSet = true;
        }
        else if (arg == "--test-mode" && i+1 < argc) {
            std::string tm = argv[++i];
            if      (tm == "negotiation") config.testMode = dummy_peer::TestMode::Negotiation;
            else if (tm == "sync")        config.testMode = dummy_peer::TestMode::Sync;
            else if (tm == "full")        config.testMode = dummy_peer::TestMode::Full;
            else if (tm == "game")        config.testMode = dummy_peer::TestMode::Game;
            else if (tm == "e2e")         config.testMode = dummy_peer::TestMode::E2E;
        }
        else if (arg == "--preset"          && i+1 < argc) { ApplyPreset(config, argv[++i]); }
        else if (arg == "--target-ip"       && i+1 < argc) { config.targetIp = argv[++i]; }
        else if (arg == "--local-port"      && i+1 < argc) { config.localPort  = static_cast<uint16_t>(std::stoi(argv[++i])); }
        else if (arg == "--remote-port"     && i+1 < argc) { config.remotePort = static_cast<uint16_t>(std::stoi(argv[++i])); }
        else if (arg == "--min-delay"       && i+1 < argc) { config.minDelayUs = std::stoll(argv[++i]) * 1000; }
        else if (arg == "--max-delay"       && i+1 < argc) { config.maxDelayUs = std::stoll(argv[++i]) * 1000; }
        else if (arg == "--loss"            && i+1 < argc) { config.packetLossRate = std::stod(argv[++i]); }
        else if (arg == "--spike-delay"     && i+1 < argc) { config.spikeDelayUs   = std::stoll(argv[++i]) * 1000; }
        else if (arg == "--spike-chance"    && i+1 < argc) { config.spikeChance    = std::stod(argv[++i]); }
        else if (arg == "--clock-offset"    && i+1 < argc) { config.clockOffsetUs  = std::stoll(argv[++i]); }
        else if (arg == "--drift-ppm"       && i+1 < argc) { config.clockDriftPpm  = std::stod(argv[++i]); }
        else if (arg == "--min-frame-time"  && i+1 < argc) { config.minFrameTimeMs = std::stoi(argv[++i]); }
        else if (arg == "--max-frame-time"  && i+1 < argc) { config.maxFrameTimeMs = std::stoi(argv[++i]); }
        else if (arg == "--duration"        && i+1 < argc) { config.testDurationSec = std::stoi(argv[++i]); }
        else if (arg == "--rollup-speed"    && i+1 < argc) { config.rollupSpeed     = std::stoi(argv[++i]); }
        else if (arg == "--disconnect-at"   && i+1 < argc) { config.disconnectAtSec   = std::stoi(argv[++i]); }
        else if (arg == "--reconnect-after" && i+1 < argc) { config.reconnectAfterSec = std::stoi(argv[++i]); }
        else if (arg == "--output-json"     && i+1 < argc) { config.outputJsonPath = argv[++i]; }
        else if (arg == "--assert-max-loss" && i+1 < argc) { config.assertMaxLossRate = std::stod(argv[++i]); }
        else if (arg == "--assert-no-gaps") { config.assertNoGaps = true; }
        // === E2E テスト固有オプション ===
        else if (arg == "--round-frames"        && i+1 < argc) { config.roundFrames       = std::stoi(argv[++i]); }
        else if (arg == "--max-rounds"          && i+1 < argc) { config.maxRounds         = std::stoi(argv[++i]); }
        else if (arg == "--rematch-choice"      && i+1 < argc) { config.rematchChoice     = std::stoi(argv[++i]); }
        else if (arg == "--auto-rematch")                        { config.autoRematch       = true; }
        else if (arg == "--chara-select-frames" && i+1 < argc) { config.charaSelectFrames = std::stoi(argv[++i]); }
    }

    std::cout << "[DummyPeer v2] Starting...\n";
    dummy_peer::DummyPeer peer(config);
    peer.Run();

    const auto& result = peer.GetResult();

    if (!config.outputJsonPath.empty()) {
        auto json = dummy_peer::ReportWriter::ToJson(result, config);
        dummy_peer::ReportWriter::WriteToFile(config.outputJsonPath, json);
    }

    bool passed = true;

    if (config.assertMaxLossRate >= 0) {
        if (result.effectiveLossRate > config.assertMaxLossRate) {
            std::cerr << "[ASSERT FAIL] Loss rate " << (result.effectiveLossRate * 100)
                      << "% exceeds max " << (config.assertMaxLossRate * 100) << "%\n";
            passed = false;
        } else {
            std::cout << "[ASSERT PASS] Loss rate OK (" << (result.effectiveLossRate * 100)
                      << "% <= " << (config.assertMaxLossRate * 100) << "%)\n";
        }
    }

    if (config.assertNoGaps) {
        if (result.frameGaps > 0 || result.frameOutOfOrder > 0) {
            std::cerr << "[ASSERT FAIL] Frame gaps=" << result.frameGaps
                      << ", out-of-order=" << result.frameOutOfOrder << "\n";
            passed = false;
        } else {
            std::cout << "[ASSERT PASS] No frame gaps or reordering detected\n";
        }
    }

    if (passed) {
        std::cout << "[DummyPeer v2] PASS - All assertions satisfied.\n";
        return 0;
    } else {
        std::cerr << "[DummyPeer v2] FAIL - One or more assertions failed.\n";
        return 1;
    }
}
