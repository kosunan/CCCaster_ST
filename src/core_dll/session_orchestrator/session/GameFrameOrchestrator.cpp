// ============================================================================
// GameFrameOrchestrator.cpp — DxHookコールバック統合（実装）
//
// 【3つの処理】
//   1. DLLロジック実行   — OnPresent()
//   2. ImGui描画          — OnEndScene()
//   3. 高速スキップ       — OnEndScene()/OnPresentSkip()
// ============================================================================

#include <windows.h>
#include "core_dll/session_orchestrator/session/GameFrameOrchestrator.hpp"
#include "core_dll/session_orchestrator/session/SceneRunner.hpp"
#include "core_dll/adapter_os_hooks/api_hook/DxHook.hpp"
#include "core_dll/adapter_os_hooks/input/InputHook.hpp"
#include "core_dll/adapter_os_hooks/input/DirectInputHook.hpp"
#include "core_dll/feature_overlay_ui/UIManager.hpp"
#include "core_dll/feature_overlay_ui/State_Ui_Logic.hpp"
#include "core_dll/adapter_netplay/SyncCoordinator.hpp"
#include "core_dll/game_memory_accessor/speed/MbaaSpeedController.hpp"
#include "core_dll/game_memory_accessor/MbaaConstants.hpp"
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <cstdio>

/// デバッグログ出力関数（dllmain.cpp で定義）
void HookLog(const char* msg);

using namespace cccaster::domain::session;

// ─── ImGui 状態管理 ─────────────────────────────────
static bool s_imguiInitialized = false;

// ─── フレーム単位描画ガード ─────────────────────────
// MBAAは1Fに~8回 EndScene を呼ぶため、ImGui描画を1F1回に制限する。
// OnPresent（1F1回保証）でカウンタを進め、OnEndScene で照合する。
static uint64_t s_presentFrameCount = 0;
static uint64_t s_lastRenderedFrame = 0;

// ============================================================================
// Register — DxHook にコールバックを登録
// ============================================================================
void GameFrameOrchestrator::Register() {
    cccaster::game_interface::DxHook::SetEndSceneCallback(OnEndScene);
    cccaster::game_interface::DxHook::SetPresentCallback(OnPresent);
    cccaster::game_interface::DxHook::SetPresentSkipCallback(OnPresentSkip);
    cccaster::game_interface::DxHook::SetPreResetCallback(OnPreReset);
    cccaster::game_interface::DxHook::SetPostResetCallback(OnPostReset);
    HookLog("[GameFrameOrchestrator] Callbacks registered to DxHook.");
}

// ============================================================================
// Shutdown — ImGui 破棄
// ============================================================================
void GameFrameOrchestrator::Shutdown() {
    if (s_imguiInitialized) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        s_imguiInitialized = false;
        HookLog("[GameFrameOrchestrator] ImGui shut down.");
    }

    // DirectInput フック解除
    cccaster::game_interface::DirectInputHook::Shutdown();
}

// ============================================================================
// OnPresent — DLLロジック実行（1F1回、確定タイミング）
// ============================================================================
//   1. SceneRunner::Step() — ゲームセッションロジック
//   2. DirectInputHook::Poll() — ジョイスティック状態取得
void GameFrameOrchestrator::OnPresent(LPDIRECT3DDEVICE9 pDevice) {
    (void)pDevice;

    // SceneRunner: ゲームスレッド上で1F分のロジック処理
    if (SceneRunner::IsReady()) {
        SceneRunner::Step();
    }

    // ジョイスティック状態を毎フレームポーリング
    cccaster::game_interface::DirectInputHook::Poll();

    // フレームカウンタ進行（EndScene描画ガード用）
    ++s_presentFrameCount;
}

// ============================================================================
// OnPresentSkip — 高速モード時は元Presentをスキップ（描画転送なし）
// ============================================================================
bool GameFrameOrchestrator::OnPresentSkip(LPDIRECT3DDEVICE9 pDevice) {
    (void)pDevice;
    return cccaster::domain::MbaaSpeedController::RenderSkip().load(std::memory_order_acquire);
}

// ============================================================================
// OnEndScene — ImGui描画（バックバッファ時のみ）+ 高速スキップ
// ============================================================================
//
// 【高速モード】
//   RenderSkip=true → ImGui描画をスキップして即リターン
//
// 【通常モード】
//   1. ImGui 遅延初期化（初回のみ）
//   2. バックバッファ判定（MBAA は 1F に ~8回 EndScene を呼ぶ）
//   3. バックバッファ一致時のみ ImGui 描画
void GameFrameOrchestrator::OnEndScene(LPDIRECT3DDEVICE9 pDevice) {
    // ── 高速モード: ImGui描画スキップ ──
    if (cccaster::domain::MbaaSpeedController::RenderSkip().load(std::memory_order_acquire)) {
        return;
    }

    // ── ImGui 遅延初期化（初回のみ）──
    if (!s_imguiInitialized) {
        HookLog("[GameFrameOrchestrator] Initializing ImGui context...");
        D3DDEVICE_CREATION_PARAMETERS params;
        pDevice->GetCreationParameters(&params);

        char buf[128];
        snprintf(buf, sizeof(buf), "[GameFrameOrchestrator] hFocusWindow: %p", params.hFocusWindow);
        HookLog(buf);

        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahoma.ttf", 14.0f);
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahoma.ttf", 24.0f);

        ImGui_ImplWin32_Init(params.hFocusWindow);
        ImGui_ImplDX9_Init(pDevice);

        cccaster::game_interface::InputHook::Initialize(params.hFocusWindow);
        cccaster::game_interface::DirectInputHook::Initialize(params.hFocusWindow);

        s_imguiInitialized = true;
        HookLog("[GameFrameOrchestrator] ImGui + InputHook initialized.");
    }

    // ── バックバッファ判定 → ImGui 描画 ──
    LPDIRECT3DSURFACE9 pRenderTarget = nullptr;
    LPDIRECT3DSURFACE9 pBackBuffer = nullptr;

    bool isBackBuffer = false;
    if (SUCCEEDED(pDevice->GetRenderTarget(0, &pRenderTarget))) {
        if (SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer))) {
            if (pRenderTarget == pBackBuffer) {
                isBackBuffer = true;
            }
            if (pBackBuffer) pBackBuffer->Release();
        }
        if (pRenderTarget) pRenderTarget->Release();
    }

    if (isBackBuffer) {
        // 1Fに1回だけ描画（MBAAは1Fに~8回EndSceneを呼ぶため）
        if (s_lastRenderedFrame == s_presentFrameCount) {
            return;
        }
        s_lastRenderedFrame = s_presentFrameCount;

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // GameMode → UiPhase 変換 → UIManager::Render
        auto uiPhase = cccaster::domain::ui::UiPhase::None;
        if (!IsBadReadPtr(CC_GAME_MODE_ADDR, sizeof(uint32_t))) {
            uint32_t gameMode = *CC_GAME_MODE_ADDR;
            switch (gameMode) {
                case CC_GAME_MODE_CHARA_SELECT:
                    uiPhase = cccaster::domain::ui::UiPhase::CharaSelect;
                    break;
                case CC_GAME_MODE_IN_GAME:
                    uiPhase = cccaster::domain::ui::UiPhase::InGame;
                    break;
                default:
                    break;
            }

            // GameMode 変化時のデバッグログ
            static uint32_t lastLoggedGameMode = 0xFFFFFFFF;
            if (gameMode != lastLoggedGameMode) {
                char buf2[128];
                snprintf(buf2, sizeof(buf2), "[GameFrameOrchestrator] GameMode changed: %u", gameMode);
                HookLog(buf2);
                lastLoggedGameMode = gameMode;
            }
        }

        // ── SyncCoordinator → オーバーレイ ステータス供給 ──
        if (cccaster::core::netplay::SyncCoordinator::GetInstance().IsRunning()) {
            auto& sync = cccaster::core::netplay::SyncCoordinator::GetInstance();
            auto& state = cccaster::core::netplay::SyncCoordinator::GetState();

            int64_t tickUs = state.currentTickUs.load(std::memory_order_relaxed);
            double fps = (tickUs > 0) ? 1000000.0 / tickUs : 60.0;
            float rttMs = sync.GetRttUs() / 1000.0f;
            float thetaMs = static_cast<float>(
                sync.GetThetaUs() - sync.GetBaselineTheta()) / 1000.0f;

            cccaster::domain::ui::StateUiLogic::SetFps(fps);
            cccaster::domain::ui::StateUiLogic::SetFrameTimeUs(tickUs);
            cccaster::domain::ui::StateUiLogic::SetTimeOffsetMs(thetaMs);
            cccaster::domain::ui::StateUiLogic::UpdateNetworkMetrics(rttMs, 0.0f);
        }

        try {
            cccaster::domain::ui::UIManager::Render(uiPhase);
        } catch (...) {
            // UI描画中のクラッシュを吸収（フレームを壊さない）
        }

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
}

// ============================================================================
// OnPreReset — Reset 前コールバック: ImGui D3D9 リソース解放
// ============================================================================
void GameFrameOrchestrator::OnPreReset(LPDIRECT3DDEVICE9 pDevice) {
    (void)pDevice;
    if (s_imguiInitialized) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }
}

// ============================================================================
// OnPostReset — Reset 後コールバック: ImGui D3D9 リソース再生成
// ============================================================================
void GameFrameOrchestrator::OnPostReset(LPDIRECT3DDEVICE9 pDevice) {
    (void)pDevice;
    if (s_imguiInitialized) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
}
