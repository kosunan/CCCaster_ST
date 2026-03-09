// ============================================================================
// NetplayManager.cpp — ネット対戦の通信管理（実装）
//
// 旧 GameHooks.cpp から UDP ソケット管理部分のみを抽出・移設。
// ============================================================================

#include "core_dll/adapter_netplay/NetplayManager.hpp"
#include "core_dll/adapter_netplay/PacketRouter.hpp"
#include "core_dll/session_orchestrator/session/DebugLog.hpp"

namespace cccaster::netplay {

// ============================================================================
// Initialize — UDP ソケット生成・バインド・受信コールバック登録
// ============================================================================
void NetplayManager::Initialize(bool isNetplay, bool isHost,
                                 uint16_t localPort, uint16_t targetPort,
                                 const std::string& targetIp) {
    _isNetplay  = isNetplay;
    _isHost     = isHost;
    _localPort  = localPort;
    _targetPort = targetPort;
    _targetIp   = targetIp;

    if (!_isNetplay) {
        cccaster::domain::session::DebugLog("[NetplayManager] 非ネットプレイモード。UDPソケットは作成しない。");
        return;
    }

    cccaster::domain::session::DebugLog("[NetplayManager] 通信対戦モードが有効です。");
    cccaster::domain::session::DebugLog("[NetplayManager]  - ロール        : %s",
                                        (_isHost ? "通信ホスト (サーバー)" : "通信クライアント"));
    cccaster::domain::session::DebugLog("[NetplayManager]  - 相手IPアドレス: %s", _targetIp.c_str());
    cccaster::domain::session::DebugLog("[NetplayManager]  - 相手ポート    : %u", _targetPort);
    cccaster::domain::session::DebugLog("[NetplayManager]  - 自バインドPort: %u", _localPort);

    try {
        _udpSocket = std::make_unique<cccaster::network::UdpSocket>(_localPort);
        _udpSocket->OnReceive([](const std::vector<uint8_t>& data,
                                  const std::string& ip, uint16_t port) {
            cccaster::core::network::PacketRouter::OnPacket(data, ip, port);
        });
        cccaster::domain::session::DebugLog(
            "[NetplayManager] UdpSocket をポート %u でバインド成功。受信ループ稼働。",
            _localPort);
    } catch (const std::exception& e) {
        cccaster::domain::session::DebugLog(
            "[NetplayManager] UdpSocket のバインドに失敗: %s", e.what());
    }
}

// ============================================================================
// Shutdown — ソケット破棄
// ============================================================================
void NetplayManager::Shutdown() {
    _udpSocket.reset();
    cccaster::domain::session::DebugLog("[NetplayManager] シャットダウン完了。");
}

} // namespace cccaster::netplay
