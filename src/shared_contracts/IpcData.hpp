#pragma once

#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace cccaster::public_api {

// Windows Shared Memory Name (Local prefix restricts to current user session)
constexpr const char* IPC_SHARED_MEM_NAME = "Local\\CCCasterV10_SharedState";
constexpr uint32_t IPC_VERSION_MAGIC = 0xCC100001;

/**
 * @brief ゲーム起動時にDLLへ指示するモードの列挙型です。
 */
enum class IpcGameMode : uint32_t {
    Versus = 0,
    Training = 1,
    Spectator = 2,
    VersusCPU = 3,
    Replay = 4
};

/**
 * @brief セッション終了時の業務エラー（切断理由）を表す列挙型です。
 */
enum class SessionErrorType : uint32_t {
    None = 0,
    SyncTimeout = 1,        // 初期同期フェーズでのタイムアウト
    PeerDisconnected = 2,   // 対戦中(Phase 3以降)の通信途絶
    AbortedByUser = 3       // ユーザーによる中断 (F12など)
};

// Strict 1-byte alignment to prevent Padding differences between 32-bit (MBAA) and 64-bit processes
#pragma pack(push, 1)
struct SharedState {
    uint32_t magicVersion;     // Magic number to verify version match
    
    // Application State
    uint32_t targetGameMode;   // 0: Versus, 1: Training, 2: Spectate
    
    // Network Info
    bool isHost;
    bool isIpv6;
    uint16_t port;
    uint16_t localPort;        // CLI が実際にバインドしたポート (Host=port, Client=0)
    char targetIp[64];         // Null-terminated string
    
    // Negotiation完了後の接続先情報 (FastBoot中にMainControllerが設定)
    char peerIp[64];           // 確定した相手のIP
    uint16_t peerPort;         // 確定した相手のPort
    
    // Game/Netplay Settings
    uint8_t delayFrames;
    uint8_t maxRollbackFrames;
    char playerName[32];       // Null-terminated string
    
    // Controller Configuration
    int32_t p1DeviceIndex;     // -1 for default, 0+ for specific device
    int32_t p2DeviceIndex;     // -1 for default, 0+ for specific device

    // Synchronization Flags
    bool dllInitialized;
    bool syncCompleted;        // true: DLL側SyncCoordinator同期完了
    bool gameShutdownRequest;
    bool headlessMode;         // true: AIテストモード (ランダム入力注入)
    
    // Error & Termination Reporting
    uint32_t lastErrorCode;    // 0 = None, 1 = Disconnect, 2 = Desync, etc.
    
    // Real-time Performance Metrics (Direct writing by DLL has ~0 overhead)
    uint32_t currentPingMs;
    uint32_t currentJitterMs;
    uint32_t totalRollbackFrames;
};
#pragma pack(pop)

#ifdef _WIN32
/**
 * @brief Inline IPC Manager avoiding the need for a separate .cpp file.
 * Safely wraps CreateFileMapping / MapViewOfFile for easy consumption by both EXE and DLL.
 */
class IpcManager {
public:
    // ---- For EXE (Creator) ----
    // Creates the shared memory and returns a handle. The handle must be kept open 
    // for the lifetime of the game process so the memory isn't destroyed by the OS.
    static HANDLE CreateAndWrite(const SharedState& state) {
        HANDLE hMapFile = CreateFileMappingA(
            INVALID_HANDLE_VALUE,    // Use paging file
            NULL,                    // Default security
            PAGE_READWRITE,          // Read/write access
            0,                       // Maximum object size (high-order DWORD)
            sizeof(SharedState),     // Maximum object size (low-order DWORD)
            IPC_SHARED_MEM_NAME);    // Name of mapping object

        if (hMapFile == NULL) {
            return NULL;
        }

        SharedState* pBuf = (SharedState*) MapViewOfFile(
            hMapFile,                // Handle to map object
            FILE_MAP_ALL_ACCESS,     // Read/write permission
            0,
            0,
            sizeof(SharedState));

        if (pBuf == NULL) {
            CloseHandle(hMapFile);
            return NULL;
        }

        // Copy data into shared memory
        std::memcpy(pBuf, &state, sizeof(SharedState));
        
        UnmapViewOfFile(pBuf);
        // Return handle to keep it alive
        return hMapFile;
    }

    // Used by EXE to update specific flags (like ShutdownRequest) or read dllInitialized
    static bool UpdateOrReadState(void (*modifierFunc)(SharedState&)) {
        HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, IPC_SHARED_MEM_NAME);
        if (hMapFile == NULL) return false;

        SharedState* pBuf = (SharedState*) MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState));
        if (pBuf == NULL) {
            CloseHandle(hMapFile);
            return false;
        }

        if (modifierFunc) {
            modifierFunc(*pBuf);
        }

        UnmapViewOfFile(pBuf);
        CloseHandle(hMapFile);
        return true;
    }

    // ---- For DLL (Reader/Updater) ----
    // Opens the existing shared memory created by EXE and reads it.
    static bool OpenAndRead(SharedState& outState) {
        HANDLE hMapFile = OpenFileMappingA(
            FILE_MAP_ALL_ACCESS,     // Read/write access
            FALSE,                   // Do not inherit the name
            IPC_SHARED_MEM_NAME);    // Name of mapping object

        if (hMapFile == NULL) {
            return false;
        }

        SharedState* pBuf = (SharedState*) MapViewOfFile(
            hMapFile,                // Handle to map object
            FILE_MAP_ALL_ACCESS,     // Read/write permission
            0,
            0,
            sizeof(SharedState));

        if (pBuf == NULL) {
            CloseHandle(hMapFile);
            return false;
        }

        bool success = false;
        if (pBuf->magicVersion == IPC_VERSION_MAGIC) {
            std::memcpy(&outState, pBuf, sizeof(SharedState));
            success = true;
        }

        UnmapViewOfFile(pBuf);
        CloseHandle(hMapFile);
        return success;
    }
};
#endif

} // namespace cccaster::public_api
