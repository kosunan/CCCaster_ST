// ============================================================================
// PacketRouter.cpp — UDP受信パケットのルーティング
//
// 【設計】
//   CC10統一ヘッダ (20B+, magic=0x30314343) を検証し、
//   全パケットを SyncCoordinator に転送する。
//   パケット種別の解釈・処理は SyncCalculator が行う。
//
//   非CC10パケットは無視する（ログ出力のみ）。
// ============================================================================
#include "core_dll/mbaa_sync/protocol/PacketRouter.hpp"
#include "core_dll/fg_netplay/sync/SyncCoordinator.hpp"
#include "core_dll/common/DebugLog.hpp"
#include <cstring>

namespace cccaster::core::network {

// CC10マジックナンバー ('CC10' little-endian)
static constexpr uint32_t CC10_MAGIC        = 0x30314343u;
static constexpr int      UNIFIED_HEADER_SIZE = 20;

// ============================================================================
// PacketRouter::OnPacket — 受信パケットのディスパッチ
// ============================================================================
void PacketRouter::OnPacket(const std::vector<uint8_t>& data, const std::string& fromIp, uint16_t fromPort) {
    if (data.empty()) return;

    // CC10統一ヘッダ検証 → SyncCoordinator に転送
    if (static_cast<int>(data.size()) >= UNIFIED_HEADER_SIZE) {
        uint32_t magic = 0;
        std::memcpy(&magic, data.data(), sizeof(magic));

        if (magic == CC10_MAGIC) {
            if (cccaster::core::netplay::SyncCoordinator::GetInstance().IsRunning()) {
                cccaster::core::netplay::SyncCoordinator::GetInstance().OnPacketReceived(data, fromIp, fromPort);
            }
            return;
        }
    }

    // 非CC10パケットは無視
    cccaster::domain::session::DebugLog(
        "[PacketRouter] UNKNOWN packet: size=%u from=%s:%u",
        (unsigned)data.size(), fromIp.c_str(), fromPort);
}

} // namespace cccaster::core::network
