#pragma once
// ============================================================================
// PacketRouter.hpp — UDP受信パケットのルーティング
//
// 仕様書: docs/requirements/dummy_peer_v2/02_packet_specification.md
//
// 【現在の対応範囲 (旧互換モード)】
//   - READY/START/PING → SyncCoordinator
//   - 2バイト生入力          → OnRemoteInputPacket (SceneRunner側)
//
// 【統一ヘッダ (20B, CC10) 対応について】
//   仕様書に定義された PacketType (CS_INPUT/LOADING_INPUT/GAME_INPUT等) の
//   ルーティングは、作業C（Domain層の入力送受信TODO実装）と
//   同一スコープで行う。PacketRouterへの変更はその時まで保留。
// ============================================================================
#include <cstdint>
#include <vector>
#include <string>

namespace cccaster::core::network {

class PacketRouter {
public:
    // UDPパケットを受信した際に呼び出す。
    // 旧互換モード:
    //   - READY/START/PING → SyncCoordinator
    //   - data.size()==2    → OnRemoteInputPacket
    static void OnPacket(const std::vector<uint8_t>& data, const std::string& fromIp, uint16_t fromPort);
};

} // namespace cccaster::core::network
