#include "NegotiationResponder.hpp"
#include "RealClock.hpp"
#include <iostream>
#include <cstring>
#include <cmath>

namespace dummy_peer {

NegotiationResponder::NegotiationResponder() {}

uint64_t NegotiationResponder::GetNowUs() const {
    return static_cast<uint64_t>(dummy_peer::GetRealQpcTimeUs());
}

std::vector<uint8_t> NegotiationResponder::HandlePacket(const uint8_t* data, int len) {
    if (len < PACKET_SIZE) {
        return {};
    }

    _connected = true;
    _packetsReceived++;

    // 受信パケットを解析
    uint16_t remoteSeq = 0;
    uint64_t remoteTimestamp = 0;
    uint64_t echoedTime = 0;
    uint64_t processingDelay = 0;

    std::memcpy(&remoteSeq, data + 0, 2);
    std::memcpy(&remoteTimestamp, data + 2, 8);
    std::memcpy(&echoedTime, data + 10, 8);
    std::memcpy(&processingDelay, data + 18, 8);

    uint64_t nowUs = GetNowUs();

    // RTT 計測: echoedTime が自分の過去のtimestampならば RTT を計算
    if (echoedTime != 0) {
        int64_t rttMicro = static_cast<int64_t>(nowUs - echoedTime) - static_cast<int64_t>(processingDelay);
        if (rttMicro < 0) rttMicro = 0;

        double pingMs = static_cast<double>(rttMicro) / 1000.0;

        if (_lastPingMs == 0.0) {
            _currentPingMs = pingMs;
            _lastPingMs = pingMs;
        } else {
            double diff = std::abs(pingMs - _lastPingMs);
            _currentJitterMs = _currentJitterMs * 0.8 + diff * 0.2;
            _currentPingMs = _currentPingMs * 0.8 + pingMs * 0.2;
            _lastPingMs = pingMs;
        }
    }

    // エコーバック用に保持
    _lastReceivedRemoteTime = remoteTimestamp;
    _lastReceiveLocalTime = nowUs;

    // 応答パケットを生成
    return CreatePacket();
}

std::vector<uint8_t> NegotiationResponder::CreatePacket() {
    std::vector<uint8_t> packet(PACKET_SIZE, 0);

    uint64_t nowUs = GetNowUs();

    uint64_t localProcessingDelay = 0;
    if (_lastReceiveLocalTime != 0) {
        localProcessingDelay = nowUs - _lastReceiveLocalTime;
    }

    std::memcpy(packet.data() + 0, &_seqNum, 2);
    std::memcpy(packet.data() + 2, &nowUs, 8);
    std::memcpy(packet.data() + 10, &_lastReceivedRemoteTime, 8);
    std::memcpy(packet.data() + 18, &localProcessingDelay, 8);

    _seqNum++;
    _packetsSent++;

    return packet;
}

void NegotiationResponder::PrintReport() const {
    std::cout << "  --- Negotiation Report ---\n";
    std::cout << "    Connected:      " << (_connected ? "Yes" : "No") << "\n";
    std::cout << "    Packets Sent:   " << _packetsSent << "\n";
    std::cout << "    Packets Recv:   " << _packetsReceived << "\n";
    std::cout << "    Current Ping:   " << _currentPingMs << " ms\n";
    std::cout << "    Current Jitter: " << _currentJitterMs << " ms\n";
}

} // namespace dummy_peer
