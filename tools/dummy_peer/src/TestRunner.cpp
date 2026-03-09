#include "TestRunner.hpp"
#include "RealClock.hpp"
#include <ws2tcpip.h>

namespace dummy_peer {

int64_t TestRunner::GetNowUs() { return static_cast<uint64_t>(dummy_peer::GetRealQpcTimeUs()); }

sockaddr_in TestRunner::MakeRemoteAddr(const std::string& ip, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    return addr;
}

} // namespace dummy_peer
