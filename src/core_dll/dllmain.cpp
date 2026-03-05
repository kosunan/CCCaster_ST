// ============================================================================
// dllmain.cpp — DLLエントリーポイント（最小構成）
//
// 責務:
//   1. DllMain: DLL_PROCESS_ATTACH/DETACH のOS橋渡し
//   2. InitThread: IPC読み取り → SessionContext構築 → フック初期化 → SceneRunner起動
//
// ★ モード分岐は行わない — SessionContext.appMode を持ち回り、
//   SceneRunner が画面状態を読み取りながらハンドリングする
// ============================================================================

#undef _mm_getcsr
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <string>
#include "core_dll/adapter_netplay/NetplayManager.hpp"
#include "core_dll/adapter_os_hooks/api_hook/TimeHooks.hpp"
#include "core_dll/game_memory_accessor/patcher/MbaaPatcher.hpp"
#include "core_dll/adapter_os_hooks/api_hook/DxHook.hpp"
#include "core_dll/session_orchestrator/session/SceneRunner.hpp"
#include "core_dll/session_orchestrator/session/SessionContext.hpp"
#include "core_dll/session_orchestrator/session/GameFrameOrchestrator.hpp"
#include "shared_contracts/IpcData.hpp"

// DLL モジュールハンドル（DllMain で最初に設定）
// HookLog がモジュールパス基準でログファイルを開くために使用する。
static HMODULE g_hModule = nullptr;

// ============================================================================
// ApplyMultiInstanceBypass — MBAA 多重起動防止バイパス
//
// MBAA.exe は起動時に以下の2つのAPIで多重起動を検出する:
//   1. FindWindowA/W — 既存ウィンドウの検出
//   2. CreateMutexA  — 排他Mutexの存在チェック (ERROR_ALREADY_EXISTS)
//
// これらのAPI関数本体を直接パッチ（インラインフック）して無効化する。
// DllMain(DLL_PROCESS_ATTACH) から呼ばれるため、ゲーム本体の初期化より先に適用される。
// ============================================================================
static void PatchFunctionBytes(void* target, const BYTE* patch, size_t size) {
    DWORD oldProtect;
    VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(target, patch, size);
    VirtualProtect(target, size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, size);
}

static void ApplyMultiInstanceBypass() {
    // FindWindowA: xor eax, eax; ret 8 → 常に NULL を返す
    FARPROC pFW = GetProcAddress(GetModuleHandleA("user32.dll"), "FindWindowA");
    if (pFW) {
        BYTE patch[] = { 0x31, 0xC0, 0xC2, 0x08, 0x00 };
        PatchFunctionBytes((void*)pFW, patch, sizeof(patch));
    }

    // FindWindowW: xor eax, eax; ret 8 → 常に NULL を返す
    FARPROC pFWW = GetProcAddress(GetModuleHandleA("user32.dll"), "FindWindowW");
    if (pFWW) {
        BYTE patch[] = { 0x31, 0xC0, 0xC2, 0x08, 0x00 };
        PatchFunctionBytes((void*)pFWW, patch, sizeof(patch));
    }

    // CreateMutexA: SetLastError(0) + mov eax, 0x1337 + ret 12
    //   → ERROR_ALREADY_EXISTS を回避し、フェイクハンドルを返す
    FARPROC pCM = GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateMutexA");
    FARPROC pSLE = GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetLastError");
    if (pCM && pSLE) {
        BYTE patch[16];
        patch[0] = 0x6A; patch[1] = 0x00;                          // push 0
        patch[2] = 0xE8;                                             // call rel32
        INT32 rel = (INT32)((BYTE*)pSLE - ((BYTE*)pCM + 2 + 5));   // relative offset
        memcpy(&patch[3], &rel, 4);
        patch[7]  = 0xB8;                                            // mov eax, imm32
        patch[8]  = 0x37; patch[9] = 0x13; patch[10] = 0x00; patch[11] = 0x00; // 0x1337
        patch[12] = 0xC2; patch[13] = 0x0C; patch[14] = 0x00;       // ret 12
        PatchFunctionBytes((void*)pCM, patch, 15);
    }
}

// ============================================================================
// HookLog — 初期化フェーズ専用ログ（DxHook 未初期化時に使用）
// ============================================================================
void HookLog(const char* msg) {
    // DLL 自身のパスを基準にログファイルパスを構築する。
    // カレントディレクトリに依存せず、常に DLL と同じフォルダ
    // （_TEST_MBAACC\cccaster\）に cccaster_hook_log.txt を出力する。
    char dllPath[MAX_PATH] = {};
    if (g_hModule) {
        GetModuleFileNameA(g_hModule, dllPath, MAX_PATH);
        // ファイル名部分を切り落としてディレクトリパスを得る
        char* lastSlash = strrchr(dllPath, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
    }
    std::string logPath = std::string(dllPath) + "cccaster_hook_log.txt";

    FILE* fp = fopen(logPath.c_str(), "a");
    if (fp) {
        fprintf(fp, "%s\n", msg);
        fclose(fp);
    }
}

// ============================================================================
// InitThread — DLL初期化スレッド
//
// DllMain(DLL_PROCESS_ATTACH) から CreateThread で起動される
// DllMain 内ではブロッキング禁止のため、全初期化をここで行う
// ============================================================================
DWORD WINAPI InitThread(LPVOID lpParam) {
    (void)lpParam;
    HookLog("=====================================");
    HookLog("[InitThread] Starting hook initialization...");

    // 高速化: 500ms → 100ms (エントリポイント同期済みのため、解凍完了済み)
    Sleep(100);

    // ================================================================
    // (1) IPC 共有メモリ読み取り → SessionContext に集約
    // ================================================================
    // ★ static: InitThread 終了後も Step() からアクセスするため永続化が必要
    static cccaster::domain::session::SessionContext ctx;
    cccaster::public_api::SharedState state;

    if (cccaster::public_api::IpcManager::OpenAndRead(state)) {
        HookLog("[InitThread] IPC Shared Memory Read SUCCESS.");

        // 起動モード
        ctx.appMode = static_cast<uint8_t>(state.targetGameMode);
        // 0=Versus, 1=Training, 2=Spectator

        // ネットワーク情報
        ctx.isHost = state.isHost;

        // 同期パラメータ（IPC値を無条件反映）
        ctx.delay       = static_cast<int16_t>(state.delayFrames);
        ctx.maxRollback = static_cast<int16_t>(state.maxRollbackFrames);



        // ネットワーク接続先情報（SceneRunner/NetplayManager が使用）
        ctx.peerPort = (state.peerPort != 0) ? state.peerPort : state.port;
        // HOST/CLIENT共にネゴシエーション時と同じポートを再利用。
        // HOST: 待機に使用した固定ポート (例: 7500)
        // CLIENT: ネゴ時にOS割当されたエフェメラルポート
        // これにより相手の peerPort と一致し、返信パケットが正しく到達する。
        ctx.localPort = state.localPort;

        // peerIp → ctx に保存（固定長配列なのでコピー）
        const char* ip = (state.peerIp[0] != '\0') ? state.peerIp : state.targetIp;
        strncpy(ctx.peerIp, ip, sizeof(ctx.peerIp) - 1);
        ctx.peerIp[sizeof(ctx.peerIp) - 1] = '\0';

        // ── 同一PC検出: CLIENT が HOST と同じポートをバインドしようとする場合、ポートをずらす ──
        // peerIp がローカルアドレス (127.x / localhost / 同一マシン) の場合に発動
        {
            std::string peerStr(ctx.peerIp);
            bool isLocalPeer = (peerStr == "127.0.0.1" || peerStr == "::1"
                             || peerStr == "localhost"
                             || peerStr.rfind("192.168.", 0) == 0
                             || peerStr.rfind("10.", 0) == 0);
            if (isLocalPeer && !ctx.isHost && ctx.localPort == ctx.peerPort) {
                ctx.localPort = ctx.peerPort + 1;
                char shiftLog[128];
                snprintf(shiftLog, sizeof(shiftLog),
                         "[InitThread] Same-PC detected: CLIENT localPort shifted %u -> %u",
                         ctx.peerPort, ctx.localPort);
                HookLog(shiftLog);
            }
        }

        // DLL初期化完了をEXEに通知
        cccaster::public_api::IpcManager::UpdateOrReadState([](cccaster::public_api::SharedState& s) {
            s.dllInitialized = true;
        });

        char log[256];
        snprintf(log, sizeof(log),
                 "[InitThread] Config -> mode=%u host=%d delay=%d maxRB=%d peer=%s:%u local=%u",
                 ctx.appMode, ctx.isHost, ctx.delay, ctx.maxRollback,
                 ctx.peerIp, ctx.peerPort, ctx.localPort);
        HookLog(log);
    } else {
        HookLog("[InitThread] IPC Shared Memory Read FAILED. Using defaults.");
    }

    // ================================================================
    // (2) MBAA 固有パッチ適用（NOP/キーボードクリア/非アクティブ判定無効化）
    // ================================================================
    HookLog("[InitThread] Applying MBAA startup patches...");
    cccaster::game_memory::MbaaPatcher::ApplyStartupPatches();

    // ================================================================
    // (3) Time API フック初期化
    // ================================================================
    HookLog("[InitThread] Initializing TimeHooks...");
    cccaster::core::hooks::TimeHooks::Initialize();

    // ================================================================
    // (4) ネットプレイ通信初期化（UDPソケット生成・受信開始）
    // ================================================================
    bool isNetplay = (ctx.appMode == 0); // Versus = netplay
    std::string peerIpStr(ctx.peerIp);

    HookLog("[InitThread] Initializing NetplayManager...");
    cccaster::netplay::NetplayManager::GetInstance().Initialize(
        isNetplay, ctx.isHost, ctx.localPort, ctx.peerPort, peerIpStr);

    // ================================================================
    // (5) DxHook 初期化
    // ================================================================
    HookLog("[InitThread] Initializing DxHook...");
    if (cccaster::game_interface::DxHook::Initialize()) {
        HookLog("[InitThread] DxHook::Initialize() SUCCEEDED");
    } else {
        HookLog("[InitThread] DxHook::Initialize() FAILED");
    }

    // ================================================================
    // (6) SceneRunner 初期化
    //
    // ★ Init() は状態変数を初期化するだけで即リターン。
    //   実際のフレーム処理 (Step()) は DxHook::Hooked_EndScene から
    //   ゲームスレッド上で呼ばれる。
    // ================================================================
    HookLog("[InitThread] Initializing SceneRunner...");
    auto sendFunc = cccaster::netplay::NetplayManager::GetInstance().GetSendFunc();
    cccaster::domain::session::SceneRunner::Init(ctx, std::move(sendFunc));

    // ================================================================
    // (7) DxHook コールバック登録
    //
    // ★ SceneRunner::Init() 完了後に登録する（R-01: 初期化順序）
    // ================================================================
    cccaster::domain::session::GameFrameOrchestrator::Register();
    HookLog("[InitThread] DxHook callbacks registered.");

    return 0;
}

// ============================================================================
// DllMain — Windowsが呼ぶDLLエントリーポイント
// ============================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;

    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule; // ← ログパス解決のため最初に設定
        DisableThreadLibraryCalls(hModule);
        ApplyMultiInstanceBypass(); // ← 多重起動バイパス（ゲーム初期化より先に適用）
        HookLog("[DllMain] DLL_PROCESS_ATTACH (multi-instance bypass applied)");
        CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        HookLog("[DllMain] DLL_PROCESS_DETACH");
        // lpReserved が nullptr でない場合、プロセス終了(ExitProcess)によるデタッチであることを示す。
        // プロセス終了時は他スレッドがすでに停止しており、Shutdownで join 等を行うとデッドロックするためスキップする。
        if (lpReserved == nullptr) {
            cccaster::domain::session::GameFrameOrchestrator::Shutdown();
            cccaster::game_interface::DxHook::Shutdown();
            cccaster::netplay::NetplayManager::GetInstance().Shutdown();
            cccaster::core::hooks::TimeHooks::Shutdown();
        } else {
            HookLog("[DllMain] Process is terminating. Skipping Shutdown() to avoid deadlock.");
        }
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
