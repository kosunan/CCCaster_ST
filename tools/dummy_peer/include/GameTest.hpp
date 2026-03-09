#pragma once
#include "TestRunner.hpp"
#include <chrono>

// Forward declarations
namespace dummy_peer { class SyncResponder; }
struct HeldInputGenerator;

namespace dummy_peer {

/**
 * Game テスト: CLI_headless + ゲーム起動の結合テスト。
 *
 * リアクティブループ方式:
 *   Negotiation後はDLLのフェーズ遷移に関知せず、
 *   SYNC_REQ/RES/DONEに常時応答 + 2バイト入力パケット60fps送信。
 *   DLLからの旧3Bシーン同期パケット(0x20/0x21/0x22)も受信・ログ記録する。
 *   DLLが CharaSelect→Loading→InGame→Rematch を何周しても自動追従。
 *
 * 使用例:
 *   DummyPeer.exe --mode client --test-mode game --duration 0
 *   (duration=0 で無制限。DLL切断 or Ctrl+C で終了)
 */
class GameTest : public TestRunner {
public:
    TestResult Run(SOCKET sock, const Config& config, std::atomic<bool>& running) override;

private:
    // 初回の SYNC_REQ/RES/DONE 交換
    bool RunInitialSync(SOCKET sock, const Config& config, std::atomic<bool>& running,
                        sockaddr_in& confirmedRemoteAddr,
                        SyncResponder& syncResp,
                        std::chrono::steady_clock::time_point endTime,
                        HeldInputGenerator& inputGen);

    // パケット受信 + SYNC関連パケットの自動応答
    // 戻り値: -1=SYNCパケット処理済み, 0=受信なし, >0=通常パケットサイズ
    int RecvAndHandleSync(SOCKET sock, sockaddr_in& confirmedRemoteAddr,
                          SyncResponder& syncResp);

    // 同期統計
    uint32_t _syncDoneSentCount   = 0;
    uint32_t _syncDoneRecvCount   = 0;
    uint32_t _syncSamplesRecv     = 0;
    int64_t  _bestRtt             = INT64_MAX;
    int64_t  _bestOffset          = 0;
    bool     _mySyncDoneSent      = false;
    std::chrono::steady_clock::time_point _lastSyncDoneRecvTime;

    // 旧3Bシーン同期パケット受信統計
    uint32_t _legacy3bCsInputRecv     = 0; ///< size==3, type==0x20 (CS_INPUT)
    uint32_t _legacy3bLoadingRecv     = 0; ///< size==3, type==0x21 (LOADING_INPUT)
    uint32_t _legacy3bRematchRecv     = 0; ///< size==3, type==0x22 (REMATCH_MENU)
};

} // namespace dummy_peer
