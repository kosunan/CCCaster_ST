#include <winsock2.h>
#include <windows.h>
#include "p2p/Session.hpp"
#include "p2p/Ntfy.hpp"
#include <iostream>
#include <future>
#include <atomic>
#include <mutex>

using namespace cccaster::p2p;
using namespace std::chrono_literals;
void Require(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(what);
}
int main() {
    try {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
        Require(NormalizeCode(" o-iL aBc ") == "011ABC", "normalization");
        Require(NormalizeCode("012ABU").empty() && NormalizeCode("12345").empty(), "invalid code");
        for (int i = 0; i < 50; ++i)
            Require(NormalizeCode(NewCode()).size() == 6, "random code");
        Require(Hex(Scrypt("", "", 16, 1)) ==
                    "77d6576238657b203b19ca42c18a0497f16b4844e3074ae8dfdffa3fede21442fcd0069ded0948f8326a753a"
                    "0fc81f17e8d3e0fb2e0d3628cf35e20c38d18906",
                "RFC7914 scrypt vector");
        Keys keys("012ABC"), again("012abc");
        Require(Hex(keys.master) == "d81068406def13b69472bbb5b1013a30f9c315e63d9e071869cdc3ddd7fc292ae243b0136ab7a7a3b931a78f192ffee3f8d7dac3d3516a94b9a21e1697b2a79e",
                "Python hashlib.scrypt interoperability");
        Require(Hex(keys.enc) == "990784c7c06fef6659f1f97b5ee08954c6b62c5e8c2eb2a8d65c88ad410dbe62" &&
                    keys.status == "3207ce2efa424424a3fd8b2cb2099523",
                "HKDF interoperability");
        Require(keys.master == again.master && keys.status == again.status, "deterministic keys");
        for (const auto& topic : {keys.status, keys.request, keys.Answer("0123456789abcdef")})
            Require(topic.size() == 32 && topic.find_first_not_of("0123456789abcdef") == std::string::npos,
                    "opaque topic without product or role names");
        Require(keys.status != keys.request && keys.request != keys.Answer("0123456789abcdef"),
                "distinct topic roles");
        std::string plain;
        auto cipher = keys.Seal(keys.request, "hello");
        Require(cipher.size() <= 4096 && keys.Open(keys.request, cipher, plain) && plain == "hello",
                "GCM roundtrip");
        Require(!keys.Open(keys.status, cipher, plain), "topic AAD");
        auto corrupt = Unbase64(cipher);
        corrupt.back() ^= 1;
        Require(!keys.Open(keys.request, Base64(corrupt), plain), "GCM tamper");
        auto session = Hex(Random(8));
        Json request = {{"v", 1}, {"type", "request"}, {"ts", Now()}, {"session", session}};
        Json decoded;
        Require(ReadMessage(keys, keys.request, keys.Seal(keys.request, request.dump()), decoded),
                "valid message");
        request["ts"] = Now() - 61;
        Require(!ReadMessage(keys, keys.request, keys.Seal(keys.request, request.dump()), decoded),
                "stale request");
        request["ts"] = Now() + 61;
        Require(!ReadMessage(keys, keys.request, keys.Seal(keys.request, request.dump()), decoded),
                "future request");
        request["ts"] = "bad";
        Require(!ReadMessage(keys, keys.request, keys.Seal(keys.request, request.dump()), decoded),
                "wrong timestamp type");
        Require(!ReadMessage(keys, keys.request, "bad!", decoded), "invalid envelope");
        std::vector<Candidate> candidates{
            {"v6", "2001:db8::5", 7500}, {"v4pub", "203.0.113.5", 49000}, {"v4lan", "192.168.1.10", 7500}},
            parsed;
        Require(ReadCandidates(Candidates(candidates), parsed) && parsed.size() == 3, "candidates roundtrip");
        Json watch = {{"v", 1},
                      {"type", "status"},
                      {"ts", Now()},
                      {"room", session},
                      {"revision", uint64_t(1)},
                      {"state", "waiting"},
                      {"spectators", {{"allowed", true}, {"candidates", Json::array()}}}};
        SpectatorStatus watchStatus;
        auto readWatch = [&] {
            return ReadSpectatorStatus(keys, keys.Seal(keys.status, watch.dump()), watchStatus);
        };
        Require(readWatch() && watchStatus.state == "waiting" && watchStatus.allowed &&
                    watchStatus.candidates.empty(),
                "spectator standby without endpoints");
        watch["state"] = "busy";
        Require(!readWatch(), "playing needs valid spectator endpoints");
        watch["spectators"]["candidates"] = Candidates(candidates);
        Require(readWatch() && watchStatus.candidates.size() == 3, "spectator candidate decode");
        watch["spectators"]["allowed"] = false;
        watch["spectators"]["candidates"] = Json::array();
        Require(readWatch() && !watchStatus.allowed, "spectator denied without IP disclosure");
        watch["ts"] = Now() - 3601;
        Require(!readWatch(), "expired spectator status");
        watch["ts"] = Now() + 61;
        Require(!readWatch(), "future spectator status");
        watch["ts"] = Now();
        watch["revision"] = -1;
        Require(!readWatch(), "negative spectator revision");
        watch["revision"] = uint64_t(2);
        watch["state"] = "closed";
        Require(readWatch() && watchStatus.state == "closed", "spectator host ended");
        auto invalid = Candidates(candidates);
        invalid[0]["a"] = "fe80::1";
        Require(!ReadCandidates(invalid, parsed), "reject link local");
        invalid = Candidates(candidates);
        invalid[1]["p"] = 65536;
        Require(!ReadCandidates(invalid, parsed), "reject invalid port");
        invalid[1]["p"] = uint64_t(4294974796ULL);
        Require(!ReadCandidates(invalid, parsed), "reject overflowing port");
        invalid = Candidates(candidates);
        invalid[1]["a"] = "127.0.0.1";
        Require(!ReadCandidates(invalid, parsed), "reject loopback");
        invalid = Json::array();
        for (int i = 0; i < 9; ++i)
            invalid.push_back(Candidates(candidates)[0]);
        Require(!ReadCandidates(invalid, parsed), "candidate cap");
        auto direct = DirectCode("012ABC", session, true, candidates);
        std::string code, sid;
        bool host = false;
        Require(ReadDirectCode(direct, code, sid, host, parsed) && code == "012ABC" && sid == session &&
                    host && parsed.size() == 3,
                "manual code roundtrip");
        Require(!ReadDirectCode(direct + "U", code, sid, host, parsed), "invalid manual code");
        Admission admission;
        for (int i = 0; i < 6; ++i)
            Require(admission.Admit(Hex(Random(8)), 10), "admission");
        Require(!admission.Admit(Hex(Random(8)), 10), "six per minute");
        Require(admission.Admit(session, 71) && !admission.Admit(session, 132), "replay rejection");
        for (int i = 0; i < 13; ++i)
            Require(admission.Admit(Hex(Random(8)), 200 + i * 61), "answer allowance");
        Require(!admission.Admit(Hex(Random(8)), 2000), "20 answer budget");
        Control control;
        auto rawSid = Unhex(session);
        std::copy(rawSid.begin(), rawSid.end(), control.session.begin());
        control.sequence = 123;
        control.type = ControlType::Select;
        control.route = "203.0.113.5/49000";
        control.host = true;
        auto wire = EncodeControl(keys.punch, control);
        Control output;
        Require(DecodeControl(keys.punch, wire, output) && output.sequence == 123 &&
                    output.route == control.route && output.host,
                "control roundtrip");
        wire[4] ^= 0x80;
        Require(!DecodeControl(keys.punch, wire, output), "role is authenticated (reflection protection)");
        wire[4] ^= 0x80;
        wire.back() ^= 1;
        Require(!DecodeControl(keys.punch, wire, output), "control MAC");
        auto tx = Random(12);
        auto stun = StunRequest(tx);
        stun[1] = 1;
        stun[0] = 1;
        stun[3] = 12;
        stun.insert(stun.end(),
                    {0, 0x20, 0, 8, 0, 1, uint8_t((49000 >> 8) ^ 0x21), uint8_t(49000 ^ 0x12),
                     uint8_t(203 ^ 0x21), uint8_t(0 ^ 0x12), uint8_t(113 ^ 0xa4), uint8_t(5 ^ 0x42)});
        Candidate mapped;
        Require(StunResponse(stun, tx, mapped) && mapped.ip == "203.0.113.5" && mapped.port == 49000,
                "STUN XOR address");
        stun[8] ^= 1;
        Require(!StunResponse(stun, tx, mapped), "STUN transaction check");
        bool badUrl = false;
        try {
            Ntfy http("http://ntfy.sh");
        } catch (...) {
            badUrl = true;
        }
        Require(badUrl, "HTTPS required");
        // 同一UDP socketを複製しても再bindせず、ポートと送受信が保たれることを確認する。
        std::cout << "socket: create\n" << std::flush;
        cccaster::network::UdpSocket original(0, false, false, true), sender(0);
        const auto port = original.GetPort();
        std::cout << "socket: duplicate port=" << port << "\n" << std::flush;
        original.OnReceive([](const auto &, const auto &, auto) {});
        auto info = original.DuplicateForProcess(GetCurrentProcessId());
        Require(!info.empty(), "WSADuplicateSocket");
        auto received = std::make_shared<std::atomic<bool>>(false);
        {
            std::cout << "socket: import\n" << std::flush;
            cccaster::network::UdpSocket imported(info);
            Require(imported.IsValid() && imported.GetPort() == port, "socket import same port");
            std::cout << "socket: receive\n" << std::flush;
            imported.OnReceive([received](const auto &bytes, const auto &, auto) {
                if (bytes == Bytes{1, 2, 3})
                    *received = true;
            });
            for (int i = 0; i < 100 && !*received; ++i) {
                sender.Send("127.0.0.1", port, {1, 2, 3});
                std::this_thread::sleep_for(10ms);
            }
            Require(*received, "imported socket receive");
        }
        *received = false;
        original.OnReceive([received](const auto &bytes, const auto &, auto) {
            if (bytes == Bytes{4, 5, 6})
                *received = true;
        });
        original.Resume();
        for (int i = 0; i < 100 && !*received; ++i) {
            sender.Send("127.0.0.1", port, {4, 5, 6});
            std::this_thread::sleep_for(10ms);
        }
        Require(*received, "launcher resumes original socket after child closes");
        original.Pause();
        std::cout << "P2P crypto, protocol, validation and socket handoff passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
