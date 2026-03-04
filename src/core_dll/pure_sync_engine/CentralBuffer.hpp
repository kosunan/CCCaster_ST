#pragma once
// ============================================================================
// CentralBuffer — 全フレーム入力管理（Central Ring Buffer）
//
// 【責務】
//   自入力・相手入力をフレームごとにセットで管理する巨大リングバッファ。
//   通信スレッド(SyncCoordinator)が書込み、ゲームスレッド(SceneRunner)が読取り。
//
// 【スレッド安全性】
//   - WriteSlot / ConfirmRemote / SetWriteHead: 通信スレッドのみ（単一writer）
//   - GetSlot / GetWriteHead / GetPlayHead: ゲームスレッドから読取り（atomic同期）
//   - ConsumeMismatch: ゲームスレッドのみ
//
// 【データフロー】
//   通信スレッド → WriteSlot() → writeHead 更新
//   DLLスレッド  ← GetSlot(playHead) → WriteInput → AdvancePlayHead
//
// 【注意】
//   CC_SKIP_FRAMES_ADDR は使用禁止。描画制御は API hook (RenderSkip) で行う。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>

namespace cccaster {
namespace core {
namespace sync {

// ── フレームスロット ──
struct FrameSlot {
    uint32_t  frame        = 0;      // フレーム番号
    uint8_t   gamePhase    = 0;      // 書込み時の画面ID (GamePhase enum)
    bool      rollbackable = false;  // ロールバック可能区域か
    uint32_t  localInput   = 0;      // 自入力（フィルタ済み・確定）
    uint32_t  remoteInput  = 0;      // 相手入力（フィルタ済み or 予測）
    bool      confirmed    = false;  // 相手入力が実パケットで確定済みか
};

class CentralBuffer {
public:
    static constexpr int RING_SIZE = 600; // 10秒分 (60fps × 10s)

    // ── シングルトン ──
    static CentralBuffer& GetInstance() {
        static CentralBuffer instance;
        return instance;
    }

    // ════════════════════════════════════════════════════
    // 通信スレッドから呼ばれる (Write系)
    // ════════════════════════════════════════════════════

    /// スロット書込み（フレーム確定時に1回呼ぶ）
    /// @param frame       フレーム番号
    /// @param gamePhase   GamePhase enum 値
    /// @param rollbackable ロールバック可能区域か
    /// @param localInput  フィルタ済み自入力
    /// @param remoteInput フィルタ済み相手入力（未確定=予測コピー）
    /// @param confirmed   相手入力が確定済みか
    void WriteSlot(uint32_t frame, uint8_t gamePhase, bool rollbackable,
                   uint32_t localInput, uint32_t remoteInput, bool confirmed) {
        auto& slot = _ring[frame % RING_SIZE];
        slot.frame        = frame;
        slot.gamePhase    = gamePhase;
        slot.rollbackable = rollbackable;
        slot.localInput   = localInput;
        slot.remoteInput  = remoteInput;
        slot.confirmed    = confirmed;
    }

    /// 相手入力を確定更新（GAME_TICK 受信時）
    /// 予測と異なる確定入力が来た場合、mismatchFrame を記録する。
    /// @param frame  確定対象フレーム
    /// @param input  確定した相手入力
    void ConfirmRemote(uint32_t frame, uint32_t input) {
        auto& slot = _ring[frame % RING_SIZE];

        // まだ未確定で、かつ予測と異なる → mismatch
        if (!slot.confirmed && slot.remoteInput != input) {
            uint32_t current = _mismatchFrame.load(std::memory_order_relaxed);
            if (current == 0 || frame < current) {
                _mismatchFrame.store(frame, std::memory_order_release);
            }
        }

        slot.remoteInput = input;
        slot.confirmed   = true;
    }

    /// writeHead（最新フレーム番号）を取得
    uint32_t GetWriteHead() const {
        return _writeHead.load(std::memory_order_acquire);
    }

    /// writeHead を設定（通信スレッドがフレーム進行時に呼ぶ）
    void SetWriteHead(uint32_t frame) {
        _writeHead.store(frame, std::memory_order_release);
    }

    // ════════════════════════════════════════════════════
    // ゲームスレッドから呼ばれる (Read系)
    // ════════════════════════════════════════════════════

    /// 指定フレームのスロットを取得（読取り専用）
    const FrameSlot& GetSlot(uint32_t frame) const {
        return _ring[frame % RING_SIZE];
    }

    /// 再生ヘッド（ゲームスレッドが再生中のフレーム）
    uint32_t GetPlayHead() const {
        return _playHead.load(std::memory_order_acquire);
    }

    /// 再生ヘッドを進める
    void AdvancePlayHead() {
        _playHead.fetch_add(1, std::memory_order_release);
    }

    /// 再生ヘッドを指定フレームに設定（ロールバック用）
    void SetPlayHead(uint32_t frame) {
        _playHead.store(frame, std::memory_order_release);
    }

    /// ロールバック判定: 予測外れが発生したフレームを返す (0=なし)
    /// 取得後にクリアされる（consume セマンティクス）
    uint32_t ConsumeMismatch() {
        return _mismatchFrame.exchange(0, std::memory_order_acq_rel);
    }

    /// 全状態リセット
    void Reset() {
        std::memset(_ring, 0, sizeof(_ring));
        _writeHead.store(0, std::memory_order_relaxed);
        _playHead.store(0, std::memory_order_relaxed);
        _mismatchFrame.store(0, std::memory_order_relaxed);
    }

private:
    CentralBuffer() = default;

    FrameSlot _ring[RING_SIZE] = {};
    std::atomic<uint32_t> _writeHead{0};       // 通信スレッド書込み位置（= 旧 currentFrame）
    std::atomic<uint32_t> _playHead{0};        // ゲームスレッド再生位置
    std::atomic<uint32_t> _mismatchFrame{0};   // 予測外れフレーム (0=なし)
};

} // namespace sync
} // namespace core
} // namespace cccaster
