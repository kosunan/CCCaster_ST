#include "core_dll/fg_netplay/common/UdpSocket.hpp"
#include "core_dll/fg_netplay/common/NetworkSimulator.hpp"

// ASIOがシステム(MinGWやvcpkg等)にインストールされている想定
// 無ければ単純なWinsockに差し替えることも可能なようにPimplで隠蔽しています
#define ASIO_STANDALONE
#include <asio.hpp>

#include <list>
#include <memory>
#include <cstdio>

namespace cccaster::network {

struct UdpSocket::Impl {
    asio::io_context ioContext;
    asio::ip::udp::socket socket;
    asio::ip::udp::endpoint remoteEndpoint; // 受信時の送信元を保持する用
    std::vector<uint8_t> recvBuffer;
    ReceiveCallback onReceiveCallback;
    bool valid = false;
    std::thread ioThread;
    asio::executor_work_guard<asio::io_context::executor_type> workGuard;

    // 遅延シミュレーション用: 非同期タイマーの寿命を保持するリスト
    std::list<std::shared_ptr<asio::steady_timer>> pendingTimers;

    Impl(uint16_t port, bool isIpv6) 
        : socket(ioContext),
          recvBuffer(4096),
          workGuard(asio::make_work_guard(ioContext))
    {
        asio::error_code ec;
        asio::ip::udp::endpoint ep(isIpv6 ? asio::ip::udp::v6() : asio::ip::udp::v4(), port);
        socket.open(ep.protocol(), ec);
        if (!ec) {
            // Options to make localhost testing on the same port possible, and dual stack friendly
            socket.set_option(asio::socket_base::reuse_address(true), ec);
            if (isIpv6) {
                socket.set_option(asio::ip::v6_only(false), ec);
            }
            
            // port 0 allows OS to pick, non-zero tries to bind specific. Check if bind succeeds.
            socket.bind(ep, ec);
            if (!ec) {
                valid = true;
                ioThread = std::thread([this]() {
                    ioContext.run();
                });
            }
        }
    }

    // 完了済みタイマーをクリーンアップ
    void CleanupTimers() {
        pendingTimers.remove_if([](const std::shared_ptr<asio::steady_timer>& t) {
            (void)t; // expired timers are cleaned up after callback
            return t.use_count() == 1; // only us holding it = callback done
        });
    }

    void DoReceive() {
        if (!socket.is_open()) return;

        socket.async_receive_from(
            asio::buffer(recvBuffer), remoteEndpoint,
            [this](const asio::error_code& error, std::size_t bytes_transferred) {
                if (!error && bytes_transferred > 0) {
                    if (onReceiveCallback) {
                        std::vector<uint8_t> data(recvBuffer.begin(), recvBuffer.begin() + bytes_transferred);
                        std::string ip = remoteEndpoint.address().to_string();
                        uint16_t port = remoteEndpoint.port();

                        auto& sim = NetworkSimulator::Instance();
                        if (sim.IsEnabled()) {
                            // パケットロス判定
                            if (sim.ShouldDrop()) {
                                // ドロップ — コールバックを呼ばない
                            } else {
                                uint32_t delayMs = sim.GetRandomDelayMs();
                                if (delayMs > 0) {
                                    // 遅延付き受信: steady_timer で遅延後にコールバック
                                    auto timer = std::make_shared<asio::steady_timer>(
                                        ioContext, std::chrono::milliseconds(delayMs));
                                    auto cb = onReceiveCallback; // コピーキャプチャ
                                    pendingTimers.push_back(timer);
                                    timer->async_wait([timer, data, ip, port, cb, this]
                                        (const asio::error_code& ec) {
                                        if (!ec && cb) {
                                            cb(data, ip, port);
                                        }
                                        CleanupTimers();
                                    });
                                } else {
                                    onReceiveCallback(data, ip, port);
                                }
                            }
                        } else {
                            // シミュレーション無効: 通常処理
                            onReceiveCallback(data, ip, port);
                        }
                    }
                }
                // エラー時（ポートクローズ等）以外は次に備えて再帰的に待受
                if (error != asio::error::operation_aborted) {
                    DoReceive();
                }
            });
    }
};

UdpSocket::UdpSocket(uint16_t bindPort, bool isIpv6) : _port(bindPort), _impl(new Impl(bindPort, isIpv6)) {
}

bool UdpSocket::IsValid() const {
    return _impl && _impl->valid;
}

uint16_t UdpSocket::GetPort() const {
    if (_impl && _impl->socket.is_open()) {
        asio::error_code ec;
        auto ep = _impl->socket.local_endpoint(ec);
        if (!ec) {
            return ep.port();
        }
    }
    return _port;
}

bool UdpSocket::IsValidIpAddress(const std::string& ip, bool isIpv6) {
    asio::error_code ec;
    auto addr = asio::ip::make_address(ip, ec);
    if (ec) return false;
    if (isIpv6 && !addr.is_v6()) return false;
    if (!isIpv6 && !addr.is_v4()) return false;
    return true;
}

UdpSocket::~UdpSocket() {
    if (_impl) {
        // コールバック呼び出し等をキャンセル
        asio::error_code ec;
        _impl->socket.close(ec);
        _impl->workGuard.reset();
        _impl->ioContext.stop();
        if (_impl->ioThread.joinable()) {
            _impl->ioThread.join();
        }
        delete _impl;
        _impl = nullptr;
    }
}

void UdpSocket::Send(const std::string& targetIp, uint16_t targetPort, const std::vector<uint8_t>& data) {
    if (!_impl || !_impl->socket.is_open()) return;

    auto& sim = NetworkSimulator::Instance();
    if (sim.IsEnabled()) {
        // パケットロス判定
        if (sim.ShouldDrop()) {
            return; // 送信しない
        }
    }

    asio::error_code ec;
    auto addr = asio::ip::make_address(targetIp, ec);
    if (!ec) {
        auto endpoint = asio::ip::udp::endpoint(addr, targetPort);
        auto bufferPtr = std::make_shared<std::vector<uint8_t>>(data);

        if (sim.IsEnabled()) {
            uint32_t delayMs = sim.GetRandomDelayMs();
            if (delayMs > 0) {
                // 遅延付き送信: steady_timer で遅延後に送信
                asio::post(_impl->ioContext, [this, endpoint, bufferPtr, delayMs]() {
                    auto timer = std::make_shared<asio::steady_timer>(
                        _impl->ioContext, std::chrono::milliseconds(delayMs));
                    _impl->pendingTimers.push_back(timer);
                    timer->async_wait([this, endpoint, bufferPtr, timer]
                        (const asio::error_code& waitEc) {
                        if (!waitEc && _impl->socket.is_open()) {
                            _impl->socket.async_send_to(
                                asio::buffer(*bufferPtr), endpoint,
                                [bufferPtr](const asio::error_code&, std::size_t) {});
                        }
                        _impl->CleanupTimers();
                    });
                });
                return;
            }
        }

        // 遅延なし or シミュレーション無効: 即時送信
        asio::post(_impl->ioContext, [this, endpoint, bufferPtr]() {
            _impl->socket.async_send_to(
                asio::buffer(*bufferPtr), endpoint,
                [bufferPtr](const asio::error_code& /*error*/, std::size_t /*bytes_transferred*/) {
                    // Buffer lifetime guaranteed until completion
                });
        });
    } else {
        // Silently skip invalid IPs during polling
    }
}

void UdpSocket::OnReceive(ReceiveCallback callback) {
    _onReceive = std::move(callback);
    
    if (_impl && _impl->socket.is_open()) {
        _impl->onReceiveCallback = _onReceive;
        // 初回の非同期受信要求を投げる
        // ioThreadが走っているので、ここから自動的にループが始まる
        asio::post(_impl->ioContext, [this]() {
            _impl->DoReceive();
        });
    }
}

// 廃止された手動ポーリングメソッド
// void UdpSocket::Poll() {
// }

} // namespace cccaster::network
