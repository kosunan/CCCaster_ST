#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include <vector>
#include <atomic>

namespace dummy_peer {

// ピアの役割
enum class PeerMode {
    Host,    // SYNC_REQを受けて応答する側（時計の基準）
    Client   // SYNC_REQを能動的に送信してθを算出する側
};

// テストモード
enum class TestMode {
    Negotiation,  // 接続確立 (26バイトPing-Pong) のみ
    Sync,         // 既存の時刻同期 + 入力テスト (デフォルト、従来互換)
    Full,         // Negotiation → Sync → Input 全フェーズ連続
    Game,         // Negotiation → 2バイト入力パケット送信 (CCCaster結合テスト用)
    E2E           // [v2 NEW] 全画面遷移の本番フロー再現テスト
};

// テスト結果構造体
struct TestResult {
    uint32_t totalFrames = 0;
    double avgFps = 0;
    uint64_t totalBurstFrames = 0;
    uint64_t packetsSent = 0;
    uint64_t packetsDropped = 0;
    double effectiveLossRate = 0;
    uint64_t frameGaps = 0;
    uint64_t frameOutOfOrder = 0;
    // 同期結果 (Clientモード時)
    bool syncCompleted = false;
    int64_t estimatedOffsetUs = 0;
    int64_t estimatedRttUs = 0;
};

// 設定構造体
struct Config {
    PeerMode mode = PeerMode::Host;
    TestMode testMode = TestMode::Sync;
    std::string targetIp = "127.0.0.1";
    uint16_t localPort = 10800;
    uint16_t remotePort = 10801;

    // ネットワークシミュレーション
    int64_t minDelayUs = 30000;   // 最小遅延 (30ms)
    int64_t maxDelayUs = 60000;   // 最大遅延 (60ms)
    double packetLossRate = 0.0;
    int64_t spikeDelayUs = 0;
    double spikeChance = 0.0;

    // クロック
    int64_t clockOffsetUs = 150000;
    double clockDriftPpm = 20.0;

    // ゲームスピード
    int minFrameTimeMs = 16;
    int maxFrameTimeMs = 30;

    // テスト時間
    int testDurationSec = 10;

    // ロールバックシミュレーション
    int rollupSpeed = 5;

    // アサーション
    double assertMaxLossRate = -1;
    bool assertNoGaps = false;

    // 切断・再接続シナリオ (Phase B-1)
    int disconnectAtSec = -1;     // -1 = 無効
    int reconnectAfterSec = -1;   // -1 = 無効

    // JSON出力 (Phase B-4)
    std::string outputJsonPath;   // 空 = 無効

    // === E2E テスト固有フィールド (v2 NEW) ===
    int roundFrames       = 3600;  // 1ラウンドのフレーム数（60秒）
    int maxRounds         = 2;     // ラウンド数
    int rematchChoice     = 0;     // 0=Rematch(再戦), 1=CharaSelect
    int rematchWaitFrames = 180;   // リマッチ画面の待ちフレーム
    bool autoRematch      = false; // true= 自動リマッチループ
    int charaSelectFrames = 300;   // キャラセレクトにかけるフレーム数
    int stageSelectFrames = 60;    // ステージ選択にかけるフレーム数
};

class DummyPeer {
public:
    explicit DummyPeer(const Config& config);
    ~DummyPeer();

    // メインループを実行（ブロッキング。testDurationSec 後に自動終了）
    void Run();

    // 強制停止
    void Stop();

    // テスト結果の取得
    const TestResult& GetResult() const { return _lastResult; }

private:
    Config _config;
    std::atomic<bool> _running{false};
    TestResult _lastResult;

    // UDP ソケット (WinSock2)
    uintptr_t _socket = 0;

    void InitSocket();
    void CloseSocket();
};

} // namespace dummy_peer
