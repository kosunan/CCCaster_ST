#include "launcher/GameLauncher.hpp"

#include <iostream>
#include <chrono>
#include <cstdint>
#include <thread>
#include <cstring>
#include <string>

namespace cccaster::main_app {

// We define our own constants to prevent breaking the build since Constants.hpp includes the missing Controller.hpp
constexpr uintptr_t CC_SKIP_FRAMES_ADDR = 0x55D25C;

GameLauncher::GameLauncher() 
    : _originalEntryPoint(0), _originalEntryPointCode(0) {
    memset(&_pi, 0, sizeof(_pi));
}

GameLauncher::~GameLauncher() {
    if (_pi.hProcess) {
        CloseHandle(_pi.hProcess);
    }
    if (_pi.hThread) {
        CloseHandle(_pi.hThread);
    }
}

/**
 * @brief 指定したメモリアドレスに対してバッファのデータを書き込むヘルパー。
 *        メモリの保護属性を一時的に PAGE_EXECUTE_READWRITE に変更して強引に書き換えます。
 */
void GameLauncher::WriteMemory(uintptr_t addr, const void* buffer, size_t size) {
    if (!_pi.hProcess) return;
    DWORD oldP;
    VirtualProtectEx(_pi.hProcess, reinterpret_cast<LPVOID>(addr), size, PAGE_EXECUTE_READWRITE, &oldP);
    WriteProcessMemory(_pi.hProcess, reinterpret_cast<LPVOID>(addr), buffer, size, nullptr);
    VirtualProtectEx(_pi.hProcess, reinterpret_cast<LPVOID>(addr), size, oldP, &oldP);
}

/**
 * @brief 指定したメモリアドレスからバッファにデータを読み込むヘルパー。
 */
void GameLauncher::ReadMemory(uintptr_t addr, void* buffer, size_t size) {
    if (!_pi.hProcess) return;
    ReadProcessMemory(_pi.hProcess, reinterpret_cast<LPCVOID>(addr), buffer, size, nullptr);
}

/**
 * @brief ゲーム(MBAA.exe)を**サスペンド状態(停止状態)**で起動します。
 *        これにより、ゲームプロセスが初期化処理（OSからのDLLロード等）を済ませた段階で
 *        ユーザースレッドの実行開始直前で停止します。この隙を利用して、DLLインジェクトや
 *        メモリ改ざんの準備（無限ループ化パッチ等）を仕掛けることができます。
 */
bool GameLauncher::LaunchSuspended(const std::string& exePath) {
    STARTUPINFOA si = { sizeof(si) };
    
    // 実行ファイルのパスからディレクトリ部分を抽出し、作業ディレクトリ(カレントディレクトリ)として設定
    std::string workDir = exePath.substr(0, exePath.find_last_of("\\/"));

    // CREATE_SUSPENDED フラグをつけてプロセス生成
    if (!CreateProcessA(NULL, (LPSTR)exePath.c_str(), NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, workDir.c_str(), &si, &_pi)) {
        return false;
    }
    return true;
}

/**
 * @brief DLLの安全なインジェクションを保証するため、一時的なエントリポイントロックを施します。
 *        ゲーム自体の改変パッチ（非アクティブ無効や描画スキップ等）はここでは「行いません」。
 *        あくまで「安全にインジェクトするまでの足止め」のみが責務です。
 */
void GameLauncher::ApplyInitialPatches() {
    // -------------------------------------------------------------------------
    // 0. エントリポイントのロック処理 (無限ループパッチ)
    // -------------------------------------------------------------------------
    // サスペンド解除後、ゲームの初期化スレッドが本来のコードを開始する前に、
    // エントリポイント(開始地点)の命令を一時的に `EB FE` (JMP $：無限ループ) に書き換えます。
    // この間、元の2バイト命令は `_originalEntryPointCode` に保持しておきます。
    DWORD peAddress = 0x400000;
    IMAGE_DOS_HEADER dosHeader;
    IMAGE_NT_HEADERS32 NTHeader;

    ReadMemory(peAddress, &dosHeader, sizeof(dosHeader));
    ReadMemory(peAddress + dosHeader.e_lfanew, &NTHeader, sizeof(NTHeader));

    // エントリポイントのアドレスを計算して元の2バイトを保存
    _originalEntryPoint = peAddress + NTHeader.OptionalHeader.AddressOfEntryPoint;
    ReadMemory(_originalEntryPoint, &_originalEntryPointCode, 2);

    // 無限ループにするための機械語 (EB FE)
    const WORD lock_code = 0xfeeb;
    WriteMemory(_originalEntryPoint, &lock_code, 2);

    // スレッドを再開(Resume)させますが、上記のパッチにより先頭で足踏みし続けます。
    // EIPレジスタ(プログラムカウンタ)がエントリポイントに到達するまで待ちます。
    CONTEXT ct;
    ct.ContextFlags = CONTEXT_CONTROL;
    int tries = 0;
    do {
        ResumeThread(_pi.hThread);
        Sleep(1); // 高速化: 10ms → 1ms (エントリポイント同期は最小待機で十分)
        SuspendThread(_pi.hThread);

        if (!GetThreadContext(_pi.hThread, &ct)) {
            if (tries++ < 500) continue;
            std::cerr << "[FastBoot] Failed to get thread context.\n";
            return;
        }
    } while (ct.Eip != _originalEntryPoint);
}

/**
 * @brief ロック状態のゲームプロセスに対してコアDLLを注入し、その後ロックを解除して処理を委譲します。
 *        ゲームの起動シーケンスの監視や、メモリ監視はすべてDLL内（GameHooks）で行われます。
 */
bool GameLauncher::MonitorBootSequence() {
    // =========================================================================
    // [計測] FastBoot タイミング計測用
    // =========================================================================
    auto bootClock = std::chrono::steady_clock::now();
    auto elapsedMs = [&]() -> long long {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - bootClock).count();
    };

    std::cerr << "[FastBoot] === Boot Timing Start ===\n";

    // -------------------------------------------------------------------------
    // コアDLL(cccaster_hook.dll)の注入(インジェクション)処理
    // -------------------------------------------------------------------------
    char myExeDir[MAX_PATH];
    GetModuleFileNameA(NULL, myExeDir, MAX_PATH);
    std::string dllPathStr = myExeDir;
    dllPathStr = dllPathStr.substr(0, dllPathStr.find_last_of("\\/")) + "\\libcccaster_hook.dll";

    void* pLibRemote = VirtualAllocEx(_pi.hProcess, NULL, dllPathStr.size() + 1, MEM_COMMIT, PAGE_READWRITE);
    if (pLibRemote) {
        WriteProcessMemory(_pi.hProcess, pLibRemote, (void*)dllPathStr.c_str(), dllPathStr.size() + 1, NULL);
        HANDLE hThread = CreateRemoteThread(_pi.hProcess, NULL, 0,
            (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "LoadLibraryA"),
            pLibRemote, 0, NULL);
        if (hThread) {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
        }
        VirtualFreeEx(_pi.hProcess, pLibRemote, 0, MEM_RELEASE);
    }
    long long tsInject = elapsedMs();
    std::cerr << "[FastBoot] [" << tsInject << "ms] DLL Injection complete\n";

    // -------------------------------------------------------------------------
    // エンジン起動の開始 (エントリポイント無限ループの解除と実行の完全移行)
    // -------------------------------------------------------------------------
    // 足止めに用いていたパッチを元に戻し、スレッドの実行を再開します。
    // ここから先、ゲーム本体のパッチ適用や状態管理は注入されたDLL(GameHooks等)が主導します。
    if (_originalEntryPoint != 0) {
        WriteMemory(_originalEntryPoint, &_originalEntryPointCode, 2);
        FlushInstructionCache(_pi.hProcess, (LPCVOID)_originalEntryPoint, 2);
    }
    ResumeThread(_pi.hThread);
    long long tsResume = elapsedMs();
    std::cerr << "[FastBoot] [" << tsResume << "ms] Entry point released, game running\n";

    // ランチャー側での FastBoot 監視はここまで（残りはコアDLL側へ移譲）
    return true;
}

/**
 * @brief GameLauncherにおける外部公開インターフェース (MainControllerから呼ばれる)
 *        一連のシーケンス(サスペンド起動 -> インジェクト準備 -> DLL注入 -> 実行再開)を完遂します。
 */
bool GameLauncher::BootAndMonitor(const std::string& exePath) {
    std::cout << "  [FastBoot] LaunchSuspended: " << exePath << std::endl;
    if (!LaunchSuspended(exePath)) {
        std::cout << "  [FastBoot] LaunchSuspended FAILED (CreateProcessA error=" << GetLastError() << ")" << std::endl;
        return false;
    }
    std::cout << "  [FastBoot] LaunchSuspended OK (PID=" << _pi.dwProcessId << ")" << std::endl;

    ApplyInitialPatches();
    std::cout << "  [FastBoot] ApplyInitialPatches OK" << std::endl;
    
    bool reachedTarget = MonitorBootSequence();
    std::cout << "  [FastBoot] MonitorBootSequence result=" << reachedTarget << std::endl;
    
    return reachedTarget;
}

} // namespace cccaster::main_app
