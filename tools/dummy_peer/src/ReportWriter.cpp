#include "ReportWriter.hpp"
#include "RealClock.hpp"
#include <sstream>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace dummy_peer {

std::string ReportWriter::ToJson(const TestResult& result, const Config& config) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);
    ss << "{\n";

    // Config セクション
    ss << "  \"config\": {\n";
    ss << "    \"mode\": \"" << (config.mode == PeerMode::Host ? "host" : "client") << "\",\n";
    ss << "    \"testMode\": \"";
    switch (config.testMode) {
        case TestMode::Negotiation: ss << "negotiation"; break;
        case TestMode::Sync:        ss << "sync"; break;
        case TestMode::Full:        ss << "full"; break;
        case TestMode::Game:        ss << "game"; break;
    }
    ss << "\",\n";
    ss << "    \"targetIp\": \"" << config.targetIp << "\",\n";
    ss << "    \"localPort\": " << config.localPort << ",\n";
    ss << "    \"remotePort\": " << config.remotePort << ",\n";
    ss << "    \"minDelayMs\": " << config.minDelayUs / 1000 << ",\n";
    ss << "    \"maxDelayMs\": " << config.maxDelayUs / 1000 << ",\n";
    ss << "    \"packetLossRate\": " << config.packetLossRate << ",\n";
    ss << "    \"spikeDelayMs\": " << config.spikeDelayUs / 1000 << ",\n";
    ss << "    \"spikeChance\": " << config.spikeChance << ",\n";
    ss << "    \"clockOffsetUs\": " << config.clockOffsetUs << ",\n";
    ss << "    \"clockDriftPpm\": " << config.clockDriftPpm << ",\n";
    ss << "    \"durationSec\": " << config.testDurationSec << "\n";
    ss << "  },\n";

    // Result セクション
    ss << "  \"result\": {\n";
    ss << "    \"totalFrames\": " << result.totalFrames << ",\n";
    ss << "    \"avgFps\": " << result.avgFps << ",\n";
    ss << "    \"totalBurstFrames\": " << result.totalBurstFrames << ",\n";
    ss << "    \"packetsSent\": " << result.packetsSent << ",\n";
    ss << "    \"packetsDropped\": " << result.packetsDropped << ",\n";
    ss << "    \"effectiveLossRate\": " << result.effectiveLossRate << ",\n";
    ss << "    \"frameGaps\": " << result.frameGaps << ",\n";
    ss << "    \"frameOutOfOrder\": " << result.frameOutOfOrder << ",\n";
    ss << "    \"syncCompleted\": " << (result.syncCompleted ? "true" : "false") << ",\n";
    ss << "    \"estimatedOffsetUs\": " << result.estimatedOffsetUs << ",\n";
    ss << "    \"estimatedRttUs\": " << result.estimatedRttUs << "\n";
    ss << "  }\n";

    ss << "}\n";
    return ss.str();
}

bool ReportWriter::WriteToFile(const std::string& path, const std::string& json) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        std::cerr << "[ReportWriter] Failed to open file: " << path << "\n";
        return false;
    }
    ofs << json;
    ofs.close();
    std::cout << "[ReportWriter] JSON report written to: " << path << "\n";
    return true;
}

} // namespace dummy_peer
