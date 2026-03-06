// ============================================================================
// DxHook.cpp — DirectX 9 関数フック（純粋インフラ層）
//
// 【責務】
//   D3D9 の EndScene / Present / Reset をフックし、
//   外部コールバックを呼び出す。
//   ImGui初期化、描画判定、ビジネスロジック等は一切持たない。
//
// 【処理フロー概要】
//   Initialize()
//     1. MinHook ライブラリ初期化
//     2. GetD3D9Device() でダミーデバイスの vtable を取得
//     3. MH_CreateHook で EndScene[42] / Reset[16] を差し替え
//     4. MH_EnableHook でフックを有効化
//
//   Hooked_EndScene() [毎フレーム呼び出し]
//     1. 初回: Present[17] を動的フック
//     2. onEndScene コールバック呼出
//     3. 元の EndScene を呼び出し
//
//   Hooked_Present() [1F1回]
//     1. onPresent コールバック呼出
//     2. onPresentSkip → true なら元の Present をスキップ
//     3. 元の Present を呼び出し
//
//   Hooked_Reset() [解像度変更/復帰時]
//     1. onPreReset コールバック呼出
//     2. 元の Reset 実行
//     3. onPostReset コールバック呼出
// ============================================================================

#include "core_dll/adapter_os_hooks/api_hook/DxHook.hpp"
#include <MinHook.h>

/// デバッグログ出力関数（dllmain.cpp で定義）
void HookLog(const char* msg);

using namespace cccaster::game_interface;

// ─── Static メンバ初期化 ───────────────────────────
void* DxHook::original_EndScene = nullptr;
void* DxHook::original_Reset = nullptr;
void* DxHook::original_Present = nullptr;
bool DxHook::isInitialized = false;
bool DxHook::isPresentHooked = false;

// ─── コールバック初期化 ────────────────────────────
DeviceCallback DxHook::onEndScene = nullptr;
DeviceCallback DxHook::onPresent = nullptr;
PresentSkipCallback DxHook::onPresentSkip = nullptr;
ResetCallback DxHook::onPreReset = nullptr;
ResetCallback DxHook::onPostReset = nullptr;

// ─── コールバック登録 ──────────────────────────────
void DxHook::SetEndSceneCallback(DeviceCallback cb) { onEndScene = cb; }
void DxHook::SetPresentCallback(DeviceCallback cb) { onPresent = cb; }
void DxHook::SetPresentSkipCallback(PresentSkipCallback cb) { onPresentSkip = cb; }
void DxHook::SetPreResetCallback(ResetCallback cb) { onPreReset = cb; }
void DxHook::SetPostResetCallback(ResetCallback cb) { onPostReset = cb; }

// ─── ダミーウィンドウプロシージャ ──────────────────
static LRESULT CALLBACK DummyWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// ============================================================================
// GetD3D9Device — ダミーデバイスを生成して vtable を取得する
// ============================================================================
bool DxHook::GetD3D9Device(void** pTable, size_t size) {
    if (!pTable) return false;

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DummyWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "DummyClass", NULL };
    RegisterClassEx(&wc);
    HWND dummyWindow = CreateWindow("DummyClass", "", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

    LPDIRECT3D9 pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        DestroyWindow(dummyWindow);
        UnregisterClass("DummyClass", wc.hInstance);
        return false;
    }

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = dummyWindow;

    LPDIRECT3DDEVICE9 pDummyDevice = nullptr;
    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dummyWindow,
                                    D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice);
    if (FAILED(hr)) {
        pD3D->Release();
        DestroyWindow(dummyWindow);
        UnregisterClass("DummyClass", wc.hInstance);
        return false;
    }

    void* pVTable = *reinterpret_cast<void**>(pDummyDevice);
    memcpy(pTable, pVTable, size);

    pDummyDevice->Release();
    pD3D->Release();
    DestroyWindow(dummyWindow);
    UnregisterClass("DummyClass", wc.hInstance);

    return true;
}

// ============================================================================
// Initialize — MinHook による D3D9 フック設定
// ============================================================================
bool DxHook::Initialize() {
    if (isInitialized) return true;

    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
        return false;
    }

    void* d3d9Device[119];
    if (GetD3D9Device(d3d9Device, sizeof(d3d9Device))) {
        void* pEndScene = d3d9Device[42];  // IDirect3DDevice9::EndScene
        void* pReset = d3d9Device[16];     // IDirect3DDevice9::Reset

        if (MH_CreateHook(pEndScene, (LPVOID)Hooked_EndScene, (reinterpret_cast<void**>(&original_EndScene))) != MH_OK) {
            return false;
        }
        if (MH_CreateHook(pReset, (LPVOID)Hooked_Reset, (reinterpret_cast<void**>(&original_Reset))) != MH_OK) {
            return false;
        }

        if (MH_EnableHook(pEndScene) != MH_OK || MH_EnableHook(pReset) != MH_OK) {
            return false;
        }

        isInitialized = true;
        return true;
    }

    return false;
}

// ============================================================================
// Shutdown — 全フック解除
// ============================================================================
void DxHook::Shutdown() {
    if (!isInitialized) return;

    // コールバックをクリア（ダングリングポインタ防止）
    onEndScene = nullptr;
    onPresent = nullptr;
    onPresentSkip = nullptr;
    onPreReset = nullptr;
    onPostReset = nullptr;

    // MinHook 全フック解除 + ライブラリ終了
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    isInitialized = false;
}

// ============================================================================
// Hooked_EndScene — EndScene フック
// ============================================================================
//   1. 初回: Present を動的フック（ゲーム実デバイスの vtable から取得）
//   2. onEndScene コールバック呼出
//   3. 元の EndScene を呼び出し
HRESULT APIENTRY DxHook::Hooked_EndScene(LPDIRECT3DDEVICE9 pDevice) {
    // ── Present 動的フック（初回のみ）──
    if (!isPresentHooked) {
        void** gameDeviceVtable = *reinterpret_cast<void***>(pDevice);
        void* gamePresentAddr = gameDeviceVtable[17];

        if (gamePresentAddr) {
            if (MH_CreateHook(gamePresentAddr, (void*)&Hooked_Present, (void**)&original_Present) == MH_OK) {
                if (MH_EnableHook(gamePresentAddr) == MH_OK) {
                    HookLog("[DxHook] Dynamically hooked Present successfully.");
                    isPresentHooked = true;
                } else {
                    HookLog("[DxHook] Failed to enable Present hook.");
                }
            } else {
                HookLog("[DxHook] Failed to CreateHook on Present.");
            }
        }
    }

    // ── コールバック呼出 ──
    if (onEndScene) {
        onEndScene(pDevice);
    }

    // ── 元の EndScene を呼び出し ──
    typedef HRESULT(APIENTRY* EndScene_t)(LPDIRECT3DDEVICE9);
    EndScene_t pOrigEndScene = (EndScene_t)original_EndScene;
    return pOrigEndScene(pDevice);
}

// ============================================================================
// Hooked_Present — Present フック（1F1回）
// ============================================================================
//   1. onPresent コールバック呼出（DLLロジック実行）
//   2. onPresentSkip → true なら元 Present をスキップ
//   3. 元の Present を呼び出し
HRESULT APIENTRY DxHook::Hooked_Present(LPDIRECT3DDEVICE9 pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) {
    // コールバック呼出（DLLロジック）
    if (onPresent) {
        onPresent(pDevice);
    }

    // 描画スキップ判定
    if (onPresentSkip && onPresentSkip(pDevice)) {
        return D3D_OK;
    }

    // 元の Present 呼び出し
    typedef HRESULT(APIENTRY* Present_t)(LPDIRECT3DDEVICE9, const RECT*, const RECT*, HWND, const RGNDATA*);
    Present_t pOrigPresent = (Present_t)original_Present;
    return pOrigPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

// ============================================================================
// Hooked_Reset — Reset フック（解像度変更/Alt+Tab復帰時）
// ============================================================================
HRESULT APIENTRY DxHook::Hooked_Reset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    // Reset 前コールバック（リソース解放）
    if (onPreReset) {
        onPreReset(pDevice);
    }

    // 元の Reset 実行
    typedef HRESULT(APIENTRY* Reset_t)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);
    Reset_t pOrigReset = (Reset_t)original_Reset;
    HRESULT hr = pOrigReset(pDevice, pPresentationParameters);

    // Reset 後コールバック（リソース再生成）
    if (onPostReset) {
        onPostReset(pDevice);
    }

    return hr;
}
