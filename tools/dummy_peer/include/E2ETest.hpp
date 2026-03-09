#pragma once
// ====================================================================
// E2ETest.hpp — E2E 全フロー再現テスト (フェーズE)
//
// TestMode::E2E 時に SceneStateMachine を駆動し、
// Negotiation → CharaSelect → Loading → InGame → Rematch の
// 全画面遷移を本番フローと同等に再現するテストクラス。
// ====================================================================

#include "DummyPeer.hpp"
#include "TestRunner.hpp"
#include "SceneStateMachine.hpp"
#include "SyncResponder.hpp"
#include "NegotiationResponder.hpp"
#include <winsock2.h>
#include <cstdint>
#include <vector>
#include <atomic>

namespace dummy_peer {

class E2ETest : public TestRunner {
public:
    explicit E2ETest(bool isHost, const Config& config,
                      SyncResponder& syncResp,
                      NegotiationResponder& negoResp);

    // ================================================================
    // Run — E2E テストのメインループ（ブロッキング）
    // ================================================================
    TestResult Run(SOCKET sock, const Config& config,
                   std::atomic<bool>& running) override;

private:
    bool                 _isHost;
    Config               _config;
    SyncResponder&       _syncResp;
    NegotiationResponder& _negoResp;
};

} // namespace dummy_peer
