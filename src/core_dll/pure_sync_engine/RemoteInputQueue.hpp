#pragma once
// ============================================================================
// RemoteInputQueue — SPSC ロックフリー リングバッファ (E-12)
//
// 【責務】
//   UDPスレッド(PacketRouter) → メインスレッド(ProcessRollbackFrame) 間で
//   リモート入力を漏れなく受け渡す。
//
// 【設計】
//   - SPSC (Single-Producer / Single-Consumer): ロック不要
//   - Producer: UDPスレッド (OnRemoteInputPacket → Push)
//   - Consumer: メインスレッド (ProcessRollbackFrame → Pop)
//   - 固定容量 256 エントリ (2のべき乗、RE.INPUT_BUFFER_SIZE と同一)
//   - _head / _tail を cache line 分離 (false sharing 防止)
//
// 【なぜ atomic 単一値ではダメか】
//   高レイテンシ環境では 1 フレーム(16.6ms)間に複数パケットが到着し、
//   atomic 単一値だと古い入力が上書きされる。このキューで全入力を蓄積。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>

namespace cccaster::sync {

/// キューに格納するエントリ
struct RemoteInputEntry {
    uint32_t frameId = 0;   ///< フレーム番号
    uint16_t input   = 0;   ///< ボタンビットマスク (FilterBlockedButtons 前)
};

/// SPSC ロックフリー リングバッファ
class RemoteInputQueue {
public:
    static constexpr uint32_t CAPACITY = 256;  // 2のべき乗必須

    /// Producer 側 (UDPスレッド): エントリを追加
    /// @return true=成功, false=キュー満杯（古いエントリが消費されていない）
    bool Push(uint32_t frameId, uint16_t input) {
        const uint32_t h = _head.load(std::memory_order_relaxed);
        const uint32_t t = _tail.load(std::memory_order_acquire);
        if (h - t >= CAPACITY) return false;  // full
        _buf[h & MASK] = { frameId, input };
        _head.store(h + 1, std::memory_order_release);
        return true;
    }

    /// Consumer 側 (メインスレッド): エントリを取り出す
    /// @return true=成功, false=キュー空
    bool Pop(RemoteInputEntry& out) {
        const uint32_t t = _tail.load(std::memory_order_relaxed);
        const uint32_t h = _head.load(std::memory_order_acquire);
        if (t == h) return false;  // empty
        out = _buf[t & MASK];
        _tail.store(t + 1, std::memory_order_release);
        return true;
    }

    /// 画面遷移時のリセット
    void Clear() {
        _head.store(0, std::memory_order_relaxed);
        _tail.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr uint32_t MASK = CAPACITY - 1;
    RemoteInputEntry _buf[CAPACITY] = {};
    alignas(64) std::atomic<uint32_t> _head{0};  // Producer 書込み位置
    alignas(64) std::atomic<uint32_t> _tail{0};  // Consumer 読取り位置
};

} // namespace cccaster::sync
