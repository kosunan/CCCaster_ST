#include "test_support.hpp"
#include "shared_contracts/IncomingRequest.hpp"

int main() {
    cccaster::notification::IncomingRequests requests;
    const std::string first = "[INCOMING_REQUEST] 0123456789abcdef\n";
    const std::string second = "[INCOMING_REQUEST] fedcba9876543210\r\n";
    CC_CASE("同じログの再読と要求の再送では重複通知しない");
    CC_CHECK_EQ(requests.Observe("[P2P_STATUS] waiting\n" + first), 1u);
    CC_CHECK_EQ(requests.Observe(first), 0u);
    CC_CHECK_EQ(requests.Observe(first + second + first), 1u);
    CC_CHECK_EQ(requests.Observe(first + second), 0u);
    CC_CASE("書き込み途中の行と不正な識別子は通知にしない");
    requests.Reset();
    CC_CHECK_EQ(requests.Observe(first.substr(0, first.size() - 1)), 0u);
    CC_CHECK_EQ(requests.Observe("text [INCOMING_REQUEST] 0123456789abcdef\n"), 0u);
    CC_CHECK_EQ(requests.Observe("[INCOMING_REQUEST] not-a-request-id\n"), 0u);
    CC_CHECK_EQ(requests.Observe("[INCOMING_REQUEST] 0123456789abcdef extra\n"), 0u);
    CC_CHECK_EQ(requests.Observe(first), 1u);
    CC_CASE("新しい待受では履歴を初期化する");
    requests.Reset();
    CC_CHECK_EQ(requests.Observe(first + second), 2u);
    return cccaster::test::Summarize("incoming_request");
}
