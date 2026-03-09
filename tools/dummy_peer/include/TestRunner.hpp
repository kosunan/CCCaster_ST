#pragma once
#include "DummyPeer.hpp"
#include <atomic>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace dummy_peer {

/**
 * テストループの基底インターフェース。
 * 各テストモード (Negotiation, Sync, Full, Game) はこのクラスを継承し、
 * Run() でメインループを実装する。
 */
class TestRunner {
public:
    virtual ~TestRunner() = default;

    /**
     * テストループを実行し、結果を返す。
     * @param sock    初期化済みUDPソケット
     * @param config  テスト設定
     * @param running 外部から停止指示を受けるフラグ
     * @return テスト結果
     */
    virtual TestResult Run(SOCKET sock, const Config& config, std::atomic<bool>& running) = 0;

protected:
    // 共通ユーティリティ: 現在時刻をマイクロ秒で取得
    static int64_t GetNowUs();

    // 共通ユーティリティ: sockaddr_in を構築
    static sockaddr_in MakeRemoteAddr(const std::string& ip, uint16_t port);
};

} // namespace dummy_peer
