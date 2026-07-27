// ============================================================================
// harness_main.cpp — ゲーム無しで同期ロジックを走らせる実行ファイル
//
// 【使い方】
//   harness.exe --host   --port 7600 --out host.txt
//   harness.exe --ip 127.0.0.1 --port 7600 --local-port 7601 --out client.txt
//
// 【何をするか】
//   FakeGame を IGameMemory として設置し、SceneRunner::Init/Step を
//   自前のループで回す。通信は本物の UDP なので、2プロセスを loopback で
//   繋ぐと実際の同期処理がそのまま動く。
//
// 【シングルトンについて】
//   SceneRunner も入力バッファもプロセス内シングルトンなので、
//   1プロセス1セッション。2セッションは2プロセスで用意する。
//
// 【実時間で動く】
//   実ゲームの Present は約60fpsで回り、SceneRunner::Step() はそこから呼ばれる。
//   ハーネスも同じ外側のペースを自前で作る（同期が成立するまで Metronome は
//   止まっているため、ペースが無いとハンドシェイクの前に走り切ってしまう）。
//   時間の仮想化は行っていないので、N フレームの実行には N/60 秒かかる。
// ============================================================================

#include "harness/FakeGame.hpp"

#include "core_dll/engine/SceneRunner.hpp"
#include "core_dll/engine/MatchContext.hpp"
#include "core_dll/mbaa_mem/IGameMemory.hpp"
#include "core_dll/network/NetplayManager.hpp"
#include "core_dll/hook/DirectInputHook.hpp"
#include "core_dll/common/ScriptedInput.hpp"
#include "core_dll/sync/NetplaySession.hpp"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace {

constexpr int64_t kFrameUs = 16666;   // 60fps

int64_t NowUs() {
    static LARGE_INTEGER freq{};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (c.QuadPart * 1000000) / freq.QuadPart;
}

/// 次フレームの期限まで待つ。Step() 内のメトロノーム待機で既に時間を
/// 使い切っていれば何もしない。
void PaceToFrame(int64_t& dueUs) {
    const int64_t now = NowUs();
    if (now < dueUs) {
        const DWORD ms = static_cast<DWORD>((dueUs - now) / 1000);
        if (ms > 0) Sleep(ms);
        while (NowUs() < dueUs) { /* 残りはスピン */ }
    }
    dueUs += kFrameUs;
    // 大きく遅れたら追いつこうとせず基準を引き直す
    const int64_t after = NowUs();
    if (dueUs < after) dueUs = after + kFrameUs;
}

struct Options {
    bool        isHost      = false;
    std::string peerIp      = "127.0.0.1";
    uint16_t    peerPort    = 7600;
    uint16_t    localPort   = 0;      // 0 なら host=peerPort / client=peerPort+1
    int16_t     delay       = 2;
    int16_t     maxRollback = 4;
    uint32_t    maxFrames   = 3000;   // 保険。IsFinished でも抜ける
    std::string outPath     = "harness_record.txt";

    cccaster::harness::FakeGame::Script script{};
};

bool ParseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::printf("[harness] %s に値がありません\n", name); return nullptr; }
            return argv[++i];
        };

        if (a == "--host") {
            o.isHost = true;
        } else if (a == "--ip") {
            const char* v = next("--ip"); if (!v) return false; o.peerIp = v;
        } else if (a == "--port") {
            const char* v = next("--port"); if (!v) return false;
            o.peerPort = static_cast<uint16_t>(std::atoi(v));
        } else if (a == "--local-port") {
            const char* v = next("--local-port"); if (!v) return false;
            o.localPort = static_cast<uint16_t>(std::atoi(v));
        } else if (a == "--delay") {
            const char* v = next("--delay"); if (!v) return false;
            o.delay = static_cast<int16_t>(std::atoi(v));
        } else if (a == "--rollback") {
            const char* v = next("--rollback"); if (!v) return false;
            o.maxRollback = static_cast<int16_t>(std::atoi(v));
        } else if (a == "--frames") {
            const char* v = next("--frames"); if (!v) return false;
            o.maxFrames = static_cast<uint32_t>(std::atoi(v));
        } else if (a == "--out") {
            const char* v = next("--out"); if (!v) return false; o.outPath = v;
        } else if (a == "--loading-frames") {
            // 左右で変えるとロード時間のばらつきを再現できる
            const char* v = next("--loading-frames"); if (!v) return false;
            o.script.loadingFrames = static_cast<uint32_t>(std::atoi(v));
        } else if (a == "--rounds") {
            const char* v = next("--rounds"); if (!v) return false;
            o.script.rounds = std::atoi(v);
        } else {
            std::printf("[harness] 不明な引数: %s\n", a.c_str());
            return false;
        }
    }

    if (o.localPort == 0) {
        o.localPort = o.isHost ? o.peerPort : static_cast<uint16_t>(o.peerPort + 1);
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!ParseArgs(argc, argv, opt)) return 2;

    std::printf("[harness] role=%s peer=%s:%u local=%u delay=%d rollback=%d loading=%uF rounds=%d\n",
                opt.isHost ? "HOST" : "CLIENT", opt.peerIp.c_str(), opt.peerPort,
                opt.localPort, opt.delay, opt.maxRollback,
                opt.script.loadingFrames, opt.script.rounds);

    // ── FakeGame を設置。これ以降 GameMem() はスクリプトされた値を返す ──
    cccaster::harness::FakeGame game(opt.script);
    cccaster::game_interface::InstallGameMemory(&game);

    // ── MatchContext 構築（dllmain が IPC から作るものを手で組む）──
    cccaster::domain::session::MatchContext ctx;
    ctx.appMode     = 0;                 // Versus
    ctx.isHost      = opt.isHost;
    ctx.peerPort    = opt.peerPort;
    ctx.localPort   = opt.localPort;
    ctx.delay       = opt.delay;
    ctx.maxRollback = opt.maxRollback;
    std::strncpy(ctx.peerIp, opt.peerIp.c_str(), sizeof(ctx.peerIp) - 1);

    // ── UDP（dllmain と同じ手順）──
    cccaster::netplay::NetplayManager::GetInstance().Initialize(
        /*isNetplay*/ true, ctx.isHost, ctx.localPort, ctx.peerPort, opt.peerIp);

    // ── 同期ロジック起動 ──
    cccaster::domain::session::SceneRunner::Init(ctx, nullptr);

    // ── メインループ（実ゲームの Present コールバックの代わり）──
    auto lastStage = game.CurrentStage();
    std::printf("[harness] stage=%s\n", game.StageName());

    timeBeginPeriod(1);              // Sleep の分解能を 1ms に上げる
    int64_t dueUs = NowUs() + kFrameUs;

    uint32_t frame = 0;
    for (; frame < opt.maxFrames && !game.IsFinished(); ++frame) {
        // ローカル入力を注入する（スタブの DirectInputHook がそのまま返す）
        const uint32_t localInput = cccaster::testing::ScriptedInput(game.Frame(), opt.isHost);
        if (opt.isHost) cccaster::game_interface::DirectInputHook::SetTestInputP1(localInput);
        else            cccaster::game_interface::DirectInputHook::SetTestInputP2(localInput);

        game.Advance();
        cccaster::domain::session::SceneRunner::Step();

        if (game.CurrentStage() != lastStage) {
            lastStage = game.CurrentStage();
            std::printf("[harness] frame=%u stage=%s\n", game.Frame(), game.StageName());
            std::fflush(stdout);
        }

        PaceToFrame(dueUs);
    }
    timeEndPeriod(1);

    std::printf("[harness] finished at frame=%u stage=%s writes=%zu\n",
                game.Frame(), game.StageName(), game.Written().size());

    cccaster::core::netplay::NetplaySession::GetInstance().Stop();

    if (!game.DumpTo(opt.outPath)) {
        std::printf("[harness] 記録の書き出しに失敗: %s\n", opt.outPath.c_str());
        return 1;
    }
    std::printf("[harness] 記録を書き出しました: %s\n", opt.outPath.c_str());
    return 0;
}
