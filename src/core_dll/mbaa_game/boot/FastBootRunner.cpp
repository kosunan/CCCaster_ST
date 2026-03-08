// ============================================================================
// FastBootRunner.cpp — ゲーム起動高速化の実装
//
// GameHooks::NetworkSyncLoop() から FastBoot フェーズを分離・移設したもの。
// ============================================================================

#include "core_dll/mbaa_game/boot/FastBootRunner.hpp"
#include "core_dll/mbaa_game/common/MemoryPatcher.hpp"
#include "core_dll/mbaa_game/constants/MbaaAddresses.hpp"
#include "core_dll/mbaa_game/constants/MbaaInputDefs.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "shared_contracts/IpcData.hpp"

#include <chrono>

namespace cccaster::game_memory {

using MP = cccaster::core::memory::MemoryPatcher;

// Static メンバ初期化
HANDLE FastBootRunner::s_hThread = nullptr;
bool   FastBootRunner::s_running = false;
cccaster::public_api::IpcGameMode FastBootRunner::s_targetMode =
    cccaster::public_api::IpcGameMode::Versus;

// ============================================================================
// Start — 裏スレッドで FastBoot を開始
// ============================================================================
void FastBootRunner::Start(cccaster::public_api::IpcGameMode targetMode) {
    if (s_running) return;
    s_running = true;
    s_targetMode = targetMode;
    s_hThread = CreateThread(nullptr, 0, ThreadFunc, nullptr, 0, nullptr);
    cccaster::domain::session::DebugLog("[FastBootRunner] スレッド起動 (targetMode=%d)",
                                        static_cast<int>(targetMode));
}

// ============================================================================
// Stop — スレッド停止
// ============================================================================
void FastBootRunner::Stop() {
    s_running = false;
    if (s_hThread) {
        WaitForSingleObject(s_hThread, 1000);
        CloseHandle(s_hThread);
        s_hThread = nullptr;
    }
    cccaster::domain::session::DebugLog("[FastBootRunner] スレッド停止");
}

// ============================================================================
// ThreadFunc — Win32 スレッドエントリポイント
// ============================================================================
DWORD WINAPI FastBootRunner::ThreadFunc(LPVOID) {
    RunLoop(s_targetMode);
    return 0;
}

// ============================================================================
// RunLoop — FastBoot ループ本体
// ============================================================================
void FastBootRunner::RunLoop(cccaster::public_api::IpcGameMode targetMode) {
    bool charSelectDetected = false;
    bool forceGotoApplied = false;
    bool toggleConfirm = false;

    // MBAA のゲーム状態遷移用定数
    constexpr uintptr_t GAME_STATE_ADDR = 0x74D598;
    constexpr uint32_t STATE_INTRO_SKIP = 101;

    while (s_running) {
        uint32_t gameMode = 0;
#if defined(_MSC_VER)
        __try {
#endif
            gameMode = MP::ReadMemory<uint32_t>(
                reinterpret_cast<uintptr_t>(CC_GAME_MODE_ADDR));
#if defined(_MSC_VER)
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
#endif

        // =================================================================
        // [フェーズ A] FastBoot: ゲーム起動 → キャラセレ到達まで
        // =================================================================
        if (!charSelectDetected && gameMode != 65535 && gameMode != 0
            && gameMode < CC_GAME_MODE_CHARA_SELECT) {
#if defined(_MSC_VER)
            __try {
#endif
                uint32_t gameState = MP::ReadMemory<uint32_t>(GAME_STATE_ADDR);
                if (gameState == 1 || gameState == 99) {
                    MP::WriteMemory<uint32_t>(GAME_STATE_ADDR, STATE_INTRO_SKIP);
                }

                // 描画スキップ: CC_SKIP_FRAMES_ADDR は使用禁止。描画制御は API hook (RenderSkip) で行う
                // MP::WriteMemory<uint32_t>(
                //     reinterpret_cast<uintptr_t>(CC_SKIP_FRAMES_ADDR), 1);

                // SFX バッファゼロクリア（起動中の不快な SE 連打防止）
                MP::ZeroMemoryRegion(reinterpret_cast<uintptr_t>(CC_SFX_ARRAY_ADDR), 1500);

                // CC_GAME_MODE_MAIN_MENU (メインメニュー画面 = 25)
                if (gameMode == 25) {
                    uint32_t inputStructAddr = MP::ReadMemory<uint32_t>(
                        reinterpret_cast<uintptr_t>(CC_PTR_TO_WRITE_INPUT_ADDR));
                    if (inputStructAddr != 0) {
                        static int menuCounter = 0;
                        static auto lastPressTime = std::chrono::steady_clock::now();
                        auto now = std::chrono::steady_clock::now();

                        uint16_t downBtn = 0x0002;    // 十字キー下
                        uint16_t confirmBtn = 0x0400;  // 決定ボタン

                        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - lastPressTime).count() > 16) {
                            uint16_t dirState = 0;
                            uint16_t btnState = 0;

                            int targetNav = (targetMode == cccaster::public_api::IpcGameMode::Training) ? 5 : 1;

                            if (menuCounter < targetNav) {
                                dirState = (toggleConfirm ? downBtn : 0);
                                if (!toggleConfirm) { menuCounter++; lastPressTime = now; }
                            } else {
                                btnState = (toggleConfirm ? confirmBtn : 0);
                                if (!toggleConfirm) lastPressTime = now;
                            }

                            MP::WriteMemory<uint16_t>(inputStructAddr + 0x18, dirState);
                            MP::WriteMemory<uint16_t>(inputStructAddr + 0x24, btnState);

                            toggleConfirm = !toggleConfirm;
                        }
                    }
                }
                // 遷移画面におけるダイレクトジャンプ命令の書き込み
                else if (!forceGotoApplied && (gameMode == 2 || gameMode == 3)) {
                    uint8_t forcePatch[2] = { 0xEB, 0x00 };

                    switch (targetMode) {
                        case cccaster::public_api::IpcGameMode::Training:  forcePatch[1] = 0x22; break;
                        case cccaster::public_api::IpcGameMode::VersusCPU: forcePatch[1] = 0x5C; break;
                        case cccaster::public_api::IpcGameMode::Versus:    forcePatch[1] = 0x3F; break;
                        case cccaster::public_api::IpcGameMode::Replay:    forcePatch[1] = 0x22; break;
                        default: forcePatch[1] = 0x3F; break;
                    }

                    MP::WritePatch(reinterpret_cast<uintptr_t>(CC_FORCE_GOTO_ADDR),
                                   forcePatch, 2);
                    forceGotoApplied = true;
                }
                // その他タイトル画面等
                else {
                    uint32_t inputStructAddr = MP::ReadMemory<uint32_t>(
                        reinterpret_cast<uintptr_t>(CC_PTR_TO_WRITE_INPUT_ADDR));
                    if (inputStructAddr != 0) {
                        uint16_t confirmBtn = toggleConfirm ? 0x0400 : 0x0000;
                        MP::WriteMemory<uint16_t>(inputStructAddr + 0x24, confirmBtn);
                        MP::WriteMemory<uint16_t>(inputStructAddr + 0x38, confirmBtn);
                    }
                    toggleConfirm = !toggleConfirm;
                }

#if defined(_MSC_VER)
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
#endif
            Sleep(1);  // FastBoot 中は 1ms ループ（最大速度）
            continue;
        } else if (gameMode == 20) {
            charSelectDetected = true;
        }

        // [フェーズ B] 通常監視: 約60FPS のポーリングループ
        Sleep(16);
    }
}

} // namespace cccaster::game_memory
