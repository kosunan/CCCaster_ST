#pragma once
#include "TestRunner.hpp"

namespace dummy_peer {

/**
 * Full テスト: Phase 1.5 (キャラセレ同期) → Phase 2 (キャラセレ入力) → Phase 4 (対戦前同期)
 * 旧 DummyPeer::FullTestLoop() を移植。
 */
class FullTest : public TestRunner {
public:
    TestResult Run(SOCKET sock, const Config& config, std::atomic<bool>& running) override;
};

} // namespace dummy_peer
