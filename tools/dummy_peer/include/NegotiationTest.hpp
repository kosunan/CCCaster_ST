#pragma once
#include "TestRunner.hpp"

namespace dummy_peer {

/**
 * Negotiation テスト: SessionNegotiator の26バイト Ping-Pong パケットによる接続確立テスト。
 * 旧 DummyPeer::NegotiationLoop() を移植。
 */
class NegotiationTest : public TestRunner {
public:
    TestResult Run(SOCKET sock, const Config& config, std::atomic<bool>& running) override;
};

} // namespace dummy_peer
