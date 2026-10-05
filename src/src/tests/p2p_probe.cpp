#include "p2p/Session.hpp"
#include "p2p/Watch.hpp"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <atomic>
#include <thread>
int main(int argc, char **argv) {
    try {
        if (argc < 4)
            return 2;
        cccaster::p2p::Options options;
        options.host = std::string(argv[1]) == "host";
        options.code = options.host ? "" : argv[1];
        options.port = uint16_t(std::stoi(argv[2]));
        options.server = argv[3];
        options.allowSpectators = !(argc > 6 && std::string(argv[6]) == "no-spectators");
        options.offline = argc > 4 && std::string(argv[4]) == "offline";
        if (argc > 4 && std::string(argv[4]) == "local") {
            options.stunServers.clear();
            options.lanDiscovery = false;
        }
        options.report = [](const auto &line) { std::cout << line << '\n' << std::flush; };
        options.manualPeer = [path = argc > 5 ? std::string(argv[5]) : std::string{}] {
            std::ifstream file(path);
            std::string text;
            std::getline(file, text);
            return text;
        };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(50);
        options.cancelled = [&] {
            return std::chrono::steady_clock::now() >= deadline || options.manualPeer() == "cancel";
        };
        if (options.code.rfind("watch:", 0) == 0) {
            auto endpoint = cccaster::p2p::WaitForSpectator(
                {options.code.substr(6), options.server, options.cancelled, options.report});
            if (endpoint.ip.empty())
                return 1;
            std::cout << "WATCH_READY " << endpoint.ip << ':' << endpoint.port << '\n' << std::flush;
            return 0;
        }
        auto result = cccaster::p2p::Connect(options);
        if (!result.socket)
            return 1;
        std::atomic<bool> received = false;
        result.socket->OnReceive([&](const auto &bytes, const auto &, auto) {
            if (bytes == cccaster::p2p::Bytes{9, 8, 7})
                received = true;
        });
        for (int i = 0; i < 100; ++i) {
            result.socket->Send(result.ip, result.port, {9, 8, 7});
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        result.socket->Pause();
        std::cout << "RESULT peer=" << result.ip << ":" << result.port
                  << " local=" << result.socket->GetPort() << " data=" << received << '\n'
                  << std::flush;
        return received ? 0 : 1;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
