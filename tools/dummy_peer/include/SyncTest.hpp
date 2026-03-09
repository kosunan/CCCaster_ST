#pragma once
#include "TestRunner.hpp"

namespace dummy_peer {

/**
 * Sync テスト: 旧 MainLoop() — 時刻同期 + 入力パケット送受信テスト。
 * NetworkSimulator経由の遅延・パケロス・スパイクをシミュレーション。
 */
class SyncTest : public TestRunner {
public:
    TestResult Run(SOCKET sock, const Config& config, std::atomic<bool>& running) override;
};

} // namespace dummy_peer
