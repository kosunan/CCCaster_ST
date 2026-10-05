// ============================================================================
// PacketRouter.cpp — UDP受信パケットのルーティング
//
// 【設計】
//   CCS1統一ヘッダ (20B+, magic=0x31534343) を検証し、
//   全パケットを NetplaySession に転送する。
//   パケット種別の解釈・処理は SyncCodec が行う。
//
//   非CCS1パケットは無視する（ログ出力のみ）。
// ============================================================================
#include "core_dll/network/PacketRouter.hpp"
#include "core_dll/sync/NetplaySession.hpp"

#include <cstring>

namespace cccaster::core::network {

// CCS1マジックナンバー ('CCS1' little-endian)
static constexpr uint32_t CCS1_MAGIC = 0x31534343u;
static constexpr int UNIFIED_HEADER_SIZE = 20;

// ============================================================================
// PacketRouter::OnPacket — 受信パケットのディスパッチ
// ============================================================================
void PacketRouter::OnPacket(const std::vector<uint8_t> &data, const std::string &fromIp, uint16_t fromPort) {
    if (data.empty())
        return;

    // CCS1統一ヘッダ検証 → NetplaySession に転送
    if (static_cast<int>(data.size()) >= UNIFIED_HEADER_SIZE) {
        uint32_t magic = 0;
        std::memcpy(&magic, data.data(), sizeof(magic));

        if (magic == CCS1_MAGIC) {
            if (cccaster::core::netplay::NetplaySession::GetInstance().IsRunning()) {
                cccaster::core::netplay::NetplaySession::GetInstance().OnPacketReceived(data, fromIp,
                                                                                        fromPort);
            }
            return;
        }
    }

    // 非CCS1パケットはサイレントドロップ（EXE SessionNegotiator 残存パケット等）
}

} // namespace cccaster::core::network
