#include "cli_launcher/controller/MainController.hpp"
#include "cli_launcher/ui/ConsoleRenderer.hpp"
#include "cli_launcher/network_wrapper/SessionNegotiator.hpp"
#include "cli_launcher/ConfigManager.hpp"
#include "launcher/GameLauncher.hpp"
#include "shared_contracts/IpcData.hpp"
#include "core_dll/adapter_network/UdpSocket.hpp"

#include <iostream>
#include <conio.h>
#include <windows.h>
#include <tlhelp32.h>
#include <chrono>

namespace cccaster::main_app::controller {

MainController::MainController(bool isHeadless, bool isIpv6, bool isHost, const std::string& targetIp, uint16_t port, const std::string& connectionHash) 
    : _currentState(AppState::MainMenu), _isHeadless(isHeadless), _isIpv6(isIpv6), _isHost(isHost), _targetIp(targetIp), _port(port), _connectionHash(connectionHash) {
    ui::ConsoleRenderer::EnableVirtualTerminalProcessing();

    if (_isHeadless) {
        _currentState = AppState::NetplayConnection;
    }
}

bool MainController::CheckGameExecutable() {
    return GetFileAttributesA("..\\MBAA.exe") != INVALID_FILE_ATTRIBUTES;
}

void MainController::ShowGameNotFoundError() {
    ui::ConsoleRenderer::ClearScreen();
    ui::ConsoleRenderer::PrintHeader();
    std::cout << "\n  \x1b[31m[ ERROR ]\x1b[0m \"MBAA.exe\" not found.\n\n"
              << "  Please ensure this tool is placed in the \"ccaster\" folder\n"
              << "  inside your MBAA game directory.\n\n"
              << "  (Press any key to return to Main Menu)\n";
    _getch();
}

void MainController::LaunchAndMonitorGame() {
    std::cout << "  \x1b[1;36m[ INFO ]\x1b[0m Launching ..\\MBAA.exe via Launcher\\GameLauncher...\n\n" << std::flush;
    
    // ---- Write IPC Shared Memory for DLL ----
    cccaster::public_api::SharedState state{};
    state.magicVersion = cccaster::public_api::IPC_VERSION_MAGIC;
    state.targetGameMode = static_cast<uint32_t>(_targetGameMode);
    state.isHost = _isHost;
    state.isIpv6 = _isIpv6;
    state.port = _port;
    state.headlessMode = _isHeadless;
    if (!_targetIp.empty() && _targetIp.length() < sizeof(state.targetIp)) {
        std::strcpy(state.targetIp, _targetIp.c_str());
    }
    // Negotiation完了後のpeer情報をIPCに書き込み
    if (!_peerIp.empty() && _peerIp.length() < sizeof(state.peerIp)) {
        std::strcpy(state.peerIp, _peerIp.c_str());
    }
    state.peerPort = _peerPort;
    state.localPort = _localPort;

    // INI設定からRollback関連の設定値を読み込んでIPCに反映
    // cccaster_v10.ini の [Netplay] セクション: DefaultDelay, MaxRollback
    state.delayFrames       = static_cast<uint8_t>(ConfigManager::GetInt("Netplay", "DefaultDelay",  2));
    state.maxRollbackFrames = static_cast<uint8_t>(ConfigManager::GetInt("Netplay", "MaxRollback",   4));
    
    // Keep handle alive until game ends or Controller exits
    HANDLE hIpc = cccaster::public_api::IpcManager::CreateAndWrite(state);
    if (!hIpc) {
        std::cout << "  \x1b[31m[ ERROR ]\x1b[0m Failed to create Shared Memory IPC bridge.\n";
    }

    // EXE自身のディレクトリを基準にMBAA.exeのパスを解決（CWD非依存）
    char myExePath[MAX_PATH];
    GetModuleFileNameA(NULL, myExePath, MAX_PATH);
    std::string exeDir(myExePath);
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));  // cccaster/
    std::string absPathStr = exeDir + "\\..\\MBAA.exe";       // cccaster/../MBAA.exe

    // 正規化（..を解決）
    char absPath[MAX_PATH];
    GetFullPathNameA(absPathStr.c_str(), MAX_PATH, absPath, nullptr);

    // ---- Boot Game and Inject DLL ----
    cccaster::main_app::GameLauncher monitor;

    if (!monitor.BootAndMonitor(absPath)) {
        std::cout << "  \x1b[31m[ ERROR ]\x1b[0m Fast boot execution failed.\n";
        if (!_isHeadless) {
            std::cout << "  (Press any key to return to Main Menu)\n";
            _getch();
        }
    } else {
        std::cout << "  \x1b[32m[ SUCCESS ]\x1b[0m Native control restored. You may now play.\n";
        
        // ===== FastBoot完了後: IPC同期監視 (12秒タイムアウト) =====
        // DLLがポートをバインドして SyncCoordinator で同期を完了するのを待つ。
        // 計測開始は Launcher の起動時点ではなく、この待ちループの開始時点とする。
        std::cout << "  \x1b[1;36m[ SYNC ]\x1b[0m Waiting for DLL sync completion (12s timeout)...\n" << std::flush;
        auto syncStart = std::chrono::steady_clock::now();
        bool syncOk = false;
        HANDLE hProcess = monitor.GetProcessHandle();

        while (true) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - syncStart).count();

            cccaster::public_api::SharedState readState{};
            if (cccaster::public_api::IpcManager::OpenAndRead(readState)) {
                if (readState.syncCompleted) {
                    syncOk = true;
                    break;
                }
            }

            // ゲームプロセスが異常終了またはDLLが自己終了(ExitGame)した場合
            if (hProcess && WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0) {
                std::cout << "  \x1b[33m[ INFO ]\x1b[0m Game process exited unexpectedly during sync.\n" << std::flush;
                break;
            }

            if (elapsed >= 15) break; // タイムアウトを12秒から15秒に少し猶予を持たせる

            Sleep(200); // 200ms間隔でポーリング
        }

        if (syncOk) {
            std::cout << "  \x1b[32m[ SYNC OK ]\x1b[0m DLL synchronization completed successfully!\n" << std::flush;
            
            // ===== ゲームプロセスの終了を監視 (UIブロッキング) =====
            std::cout << "  \x1b[1;36m[ IN GAME ]\x1b[0m Playing match. Waiting for game process to exit...\n" << std::flush;
            if (hProcess) {
                // ゲーム終了まで待機
                WaitForSingleObject(hProcess, INFINITE);
            }
            std::cout << "  \x1b[1;36m[ INFO ]\x1b[0m Game process exited.\n" << std::flush;
        } else {
            // syncOkがfalse、かつゲームがまだ稼働中なら12秒の同期タイムアウトとみなして強制終了する
            if (hProcess && WaitForSingleObject(hProcess, 0) != WAIT_OBJECT_0) {
                std::cout << "  \x1b[31m[ SYNC TIMEOUT ]\x1b[0m DLL sync did not complete within 12 seconds.\n" << std::flush;
                std::cout << "  \x1b[31m[ ABORT ]\x1b[0m Terminating game process...\n" << std::flush;
                TerminateProcess(hProcess, 1);
            }
        }

    // ===== 異常切断等の業務エラー確認 (必ず実施) =====
    cccaster::public_api::SharedState finalState{};
    if (cccaster::public_api::IpcManager::OpenAndRead(finalState)) {
        auto err = static_cast<cccaster::public_api::SessionErrorType>(finalState.lastErrorCode);
        if (err != cccaster::public_api::SessionErrorType::None) {
            ui::ConsoleRenderer::ClearScreen();
            ui::ConsoleRenderer::PrintHeader();
            std::cout << "\n  \x1b[1;31m====== SESSION TERMINATED WITH ERROR ======\x1b[0m\n\n";
            switch (err) {
                case cccaster::public_api::SessionErrorType::SyncTimeout:
                    std::cout << "  [\x1b[31m Sync Timeout \x1b[0m] Initial synchronization failed or timed out.\n";
                    break;
                case cccaster::public_api::SessionErrorType::PeerDisconnected:
                    std::cout << "  [\x1b[31m Peer Disconnected \x1b[0m] Connection lost during the match (3s timeout).\n";
                    break;
                case cccaster::public_api::SessionErrorType::AbortedByUser:
                    std::cout << "  [\x1b[33m User Aborted \x1b[0m] The match was aborted locally (F12 pressed).\n";
                    break;
                default:
                    std::cout << "  [\x1b[31m Unknown Error \x1b[0m] Code: " << finalState.lastErrorCode << "\n";
                    break;
            }
            std::cout << "\n  ===========================================\n\n";
            std::cout << "  Press any key to return to Main Menu...\n";
            _getch();
        } else if (syncOk) {
            if (!_isHeadless) std::cout << "  \x1b[32m[ OK ]\x1b[0m Match finished normally.\n";
        }
    }
    } // <-- Missing brace closed

    if (hIpc) {
        // We do not close the IPC handle yet if the game relies on it later, 
        // but since DLL already read it inside BootAndMonitor's injection phase, 
        // closing it here is safe if we don't plan to poll dllInitialized.
        // For polling, we would keep it as a class member.
        CloseHandle(hIpc);
    }

    // Fix 2: Clean up terminal state and application variables post-game
    ui::ConsoleRenderer::EnableVirtualTerminalProcessing();
    ui::ConsoleRenderer::ClearScreen();
    _isHost = false;
    _isIpv6 = false;
}

void MainController::Run() {
    while (_currentState != AppState::Exit) {
        try {
            switch (_currentState) {
                case AppState::MainMenu:
                    HandleMainMenu();
                    break;
                // NetplayHostOrClient は廃止（入力内容で自動判別）
                case AppState::NetplayConnection:
                    HandleNetplayConnection();
                    break;
                case AppState::Spectating_WaitingForHost:
                    HandleSpectateConnect();
                    break;
                case AppState::GameRunning:
                    LaunchAndMonitorGame();
                    _currentState = AppState::MainMenu;
                    break;
                default:
                    _currentState = AppState::Exit;
                    break;
            }
        } catch (const std::exception& e) {
            ui::ConsoleRenderer::ClearScreen();
            std::cout << "\n  \x1b[31m[ FATAL ERROR ]\x1b[0m An unexpected error occurred:\n"
                      << "  " << e.what() << "\n\n"
                      << "  Returning to Main Menu. Press any key to continue...\n";
            _getch();
            _currentState = AppState::MainMenu;
        } catch (...) {
            ui::ConsoleRenderer::ClearScreen();
            std::cout << "\n  \x1b[31m[ FATAL ERROR ]\x1b[0m An unknown error occurred.\n\n"
                      << "  Returning to Main Menu. Press any key to continue...\n";
            _getch();
            _currentState = AppState::MainMenu;
        }
    }
}

void MainController::HandleMainMenu() {
    std::vector<MenuOption> options = {
        {"Netplay              [Hash Connect]", [self = this]() {
            if (!self) return;
            if (!self->CheckGameExecutable()) { self->ShowGameNotFoundError(); return; }
            self->_targetGameMode = cccaster::public_api::IpcGameMode::Versus;
            self->_currentState = AppState::NetplayConnection;
        }},
        {"Training Mode        [Offline]", [self = this]() {
            if (!self) return;
            if (!self->CheckGameExecutable()) { self->ShowGameNotFoundError(); return; }
            ui::ConsoleRenderer::ClearScreen();
            ui::ConsoleRenderer::PrintHeader();
            std::cout << "\n  Launching Offline Training Mode...\n";
            self->_targetGameMode = cccaster::public_api::IpcGameMode::Training;
            self->_currentState = AppState::GameRunning;
        }},
        {"Spectate             [Watch a Match]", [self = this]() {
            if (!self) return;
            if (!self->CheckGameExecutable()) { self->ShowGameNotFoundError(); return; }
            self->_targetGameMode = cccaster::public_api::IpcGameMode::Spectator;
            self->_currentState = AppState::Spectating_WaitingForHost;
        }},
        {"Exit", [self = this]() {
            if (!self) return;
            self->_currentState = AppState::Exit;
        }}
    };

    std::vector<std::string> labels;
    for (const auto& opt : options) labels.push_back(opt.label);

    int choice = ui::ConsoleRenderer::DrawMenuAndGetSelection("MAIN MENU", labels, false);
    if (choice >= 0 && choice < static_cast<int>(options.size())) {
        options[choice].onSelect();
    }
}

void MainController::HandleNetplayConnection() {
    ui::ConsoleRenderer::ClearScreen();
    ui::ConsoleRenderer::PrintHeader();

    std::cout << "\n  Hash-Based Network Play\n"
              << "  ======================================================\n\n";

    network_wrapper::SessionNegotiator negotiator;
    network_wrapper::NegotiationResult negoResult;

    if (_isHeadless) {
        // === ヘッドレスモード: 既存CLI引数ベースの分岐をそのまま使用 ===
        if (_isHost) {
            uint16_t port = (_port > 0) ? _port : 7500;
            std::string hash = network_wrapper::SessionNegotiator::GenerateConnectionHash(port);
            std::cout << "  [HEADLESS HOST] Hash: " << hash << "\n";
            negoResult = negotiator.RunNegotiation(false, true, "", port, true, true);
        } else if (!_connectionHash.empty()) {
            negoResult = negotiator.RunNegotiationFromHash(_connectionHash);
        } else if (!_targetIp.empty()) {
            negoResult = negotiator.RunNegotiation(_isIpv6, false, _targetIp, _port, true);
        } else {
            std::cout << "  \x1b[31m[ HEADLESS ERROR ]\x1b[0m No --host, --hash, or --ip specified.\n";
            _currentState = AppState::Exit;
            return;
        }
    } else {
        // === インタラクティブモード: 統合入力 ===
        // ポート番号入力 → HOST自動判別 / ハッシュ貼り付け → CLIENT自動判別
        auto inputOpt = ui::ConsoleRenderer::GetTextInputWithCancel(
            "Enter Port (HOST) or Paste Hash (JOIN)  [Empty = HOST:7500]",
            "", true);

        // ESCキャンセル → メインメニューに戻る
        if (!inputOpt.has_value()) {
            _currentState = AppState::MainMenu;
            return;
        }

        std::string input = inputOpt.value();

        // === 入力値の自動判別 ===
        // (1) 空文字列 → デフォルトポート(7500)でHOSTモード
        // (2) 全文字が数字[0-9]で0〜65535の範囲 → 指定ポートでHOSTモード
        // (3) それ以外 → ハッシュ文字列としてCLIENTモード
        bool isPortInput = true;
        if (!input.empty()) {
            if (input.length() > 5) {
                isPortInput = false;
            } else {
                for (char c : input) {
                    if (c < '0' || c > '9') {
                        isPortInput = false;
                        break;
                    }
                }
                if (isPortInput) {
                    int val = std::stoi(input);
                    if (val < 0 || val > 65535) {
                        isPortInput = false;
                    }
                }
            }
        }

        if (isPortInput) {
            // === HOSTモード ===
            _isHost = true;
            uint16_t port = input.empty() ? 7500 : static_cast<uint16_t>(std::stoi(input));

            // ハッシュ生成（IPv4+IPv6同時取得）
            std::string hash = network_wrapper::SessionNegotiator::GenerateConnectionHash(port);

            std::cout << "\n  \x1b[1;32m[ CONNECTION HASH ]\x1b[0m\n";
            std::cout << "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
            std::cout << "  \x1b[1;33m" << hash << "\x1b[0m\n";
            std::cout << "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
            std::cout << "  \x1b[32m[ INFO ]\x1b[0m Hash copied to clipboard. Share with opponent.\n\n";

            // クリップボードにコピー
            negotiator.CopyToClipboard(hash);

            // ハッシュモード: skipHostDisplay=true で RunNegotiation 内の
            // グローバルIP再取得・画面クリア・クリップボード上書きをスキップ
            negoResult = negotiator.RunNegotiation(false, true, "", port, false, true);
        } else {
            // === CLIENTモード（ハッシュ接続） ===
            _isHost = false;
            negoResult = negotiator.RunNegotiationFromHash(input);
        }
    }

    if (negoResult.success) {
        // peer情報を保持 (FastBoot中の中継用)
        _peerIp = negoResult.peerIp;
        _peerPort = negoResult.peerPort;
        _localPort = negoResult.localPort;
        if (_isHeadless) {
            std::cout << "  \x1b[32m[ HEADLESS ]\x1b[0m Connection established successfully. Booting game...\n";
        }
        _currentState = AppState::GameRunning;
    } else {
        if (_isHeadless) {
            std::cout << "  \x1b[31m[ HEADLESS ERROR ]\x1b[0m Connection failed.\n";
            _currentState = AppState::Exit;
        } else {
            _currentState = AppState::MainMenu;
        }
    }
}

void MainController::HandleSpectateConnect() {
    ui::ConsoleRenderer::ClearScreen();
    ui::ConsoleRenderer::PrintHeader();
    std::cout << "\n  Spectate Mode\n";
    std::cout << "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    auto ipOpt = ui::ConsoleRenderer::GetTextInputWithCancel("Enter Host IP Address:", "", true);

    if (!ipOpt.has_value() || ipOpt.value().empty()) {
        _currentState = AppState::MainMenu;
        return;
    }

    std::cout << "Connecting as Spectator...\n";
    ui::ConsoleRenderer::GetTextInputWithCancel("Press ENTER to simulate spectator game launch...", "", false);

    _currentState = AppState::GameRunning;
}

} // namespace cccaster::main_app::controller

