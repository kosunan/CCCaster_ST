#pragma once
// ============================================================================
// MatchInputBuffer — 対戦中(InGame)用入力管理バッファ
//
// 【責務】
//   InGame フェーズにおける格闘ゲームのコマンドなど、重要かつ厳密な同期が必要な
//   入力を管理する。WT（ワールドタイム）基準の相対フレームを用いた厳密なペアリングと、
//   予測外れ時のロールバック（Mismatch監視）をサポートする。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>

namespace cccaster {
namespace core {
namespace sync {

// ── 対戦用フレームスロット ──
struct MatchFrameSlot {
    uint32_t  frame        = 0;      // WTベースの相対フレーム番号
    bool      rollbackable = false;  // ロールバック可能区域か
    uint32_t  localInput   = 0;      // 自入力（確定）
    uint32_t  remoteInput  = 0;      // 相手入力（予測 or パケットによる確定）
    bool      confirmed    = false;  // 相手入力が実パケットで確定済みか
};

class MatchInputBuffer {
public:
    static constexpr int RING_SIZE = 600; // 10秒分 (60fps × 10s)

    // ── シングルトン ──
    static MatchInputBuffer& GetInstance() {
        static MatchInputBuffer instance;
        return instance;
    }

    // ════════════════════════════════════════════════════
    // Write系
    // ════════════════════════════════════════════════════

    void WriteSlot(uint32_t frame, bool rollbackable,
                   uint32_t localInput, uint32_t remoteInput, bool confirmed) {
        auto& slot = _ring[frame % RING_SIZE];
        slot.frame        = frame;
        slot.rollbackable = rollbackable;
        slot.localInput   = localInput;
        slot.remoteInput  = remoteInput;
        slot.confirmed    = confirmed;
    }

    void ConfirmRemote(uint32_t frame, uint32_t input) {
        auto& slot = _ring[frame % RING_SIZE];

        // 予測外れ（mismatch）の検知: まだ未確定で予測と違う場合
        if (!slot.confirmed && slot.remoteInput != input) {
            uint32_t current = _mismatchFrame.load(std::memory_order_relaxed);
            if (current == 0 || frame < current) {
                // 最も古いミスマッチフレームを保持する
                _mismatchFrame.store(frame, std::memory_order_release);
            }
        }

        slot.remoteInput = input;
        slot.confirmed   = true;

        uint32_t prev = _confirmedRemoteFrame.load(std::memory_order_relaxed);
        if (frame > prev) {
            _confirmedRemoteFrame.store(frame, std::memory_order_release);
        }
    }

    uint32_t GetWriteHead() const {
        return _writeHead.load(std::memory_order_acquire);
    }

    void SetWriteHead(uint32_t frame) {
        _writeHead.store(frame, std::memory_order_release);
    }

    // ════════════════════════════════════════════════════
    // 初期化・パラメータ制御
    // ════════════════════════════════════════════════════

    void Initialize(uint32_t startFrame, int16_t delay, int16_t maxRollback) {
        Reset();
        SetWriteHead(startFrame);
        InitializeConfirmedRemoteFrame(startFrame);
        SetSyncParams(delay, maxRollback);
    }

    void SetSyncParams(int16_t delay, int16_t maxRollback) {
        _delay = delay;
        _maxRollback = maxRollback;
    }

    int16_t GetDelay() const { return _delay; }
    int16_t GetMaxRollback() const { return _maxRollback; }

    // ════════════════════════════════════════════════════
    // Read系
    // ════════════════════════════════════════════════════

    /// @brief ゲーム入力の完全パイプライン
    bool ReadFrameForGame(bool isHost, uint32_t& p1, uint32_t& p2) const {
        uint32_t readPos = GetReadPos();
        if (readPos == 0) return false;
        const auto& slot = _ring[readPos % RING_SIZE];
        if (!slot.confirmed) return false;

        if (isHost) { p1 = slot.localInput; p2 = slot.remoteInput; }
        else        { p1 = slot.remoteInput; p2 = slot.localInput; }
        return true;
    }

    /// @brief 読取位置（D+R 補正）の算出
    uint32_t GetReadPos() const {
        uint32_t wh = _writeHead.load(std::memory_order_acquire);
        int32_t offset = static_cast<int32_t>(_delay) + static_cast<int32_t>(_maxRollback);
        if (offset < 1) offset = 1;  // 最低1フレーム遅延
        int32_t pos = static_cast<int32_t>(wh) - offset;
        return (pos >= 0) ? static_cast<uint32_t>(pos) : 0;
    }

    uint32_t GetConfirmedRemoteFrame() const {
        return _confirmedRemoteFrame.load(std::memory_order_acquire);
    }

    /// @brief 実効上の最新進行可能フレーム（相手のパケット到達に依存）
    uint32_t GetEffectiveHead() const {
        uint32_t wh = _writeHead.load(std::memory_order_acquire);
        int32_t offset = static_cast<int32_t>(_delay) + static_cast<int32_t>(_maxRollback);
        if (offset < 1) offset = 1;

        int32_t delayAdjusted = static_cast<int32_t>(wh) - offset;
        if (delayAdjusted < 0) delayAdjusted = 0;

        uint32_t confirmed = _confirmedRemoteFrame.load(std::memory_order_acquire);
        uint32_t da = static_cast<uint32_t>(delayAdjusted);

        if (confirmed == 0) return da;
        return (da < confirmed) ? da : confirmed;
    }

    const MatchFrameSlot& GetSlot(uint32_t frame) const {
        return _ring[frame % RING_SIZE];
    }

    /// @brief ロールバック判定用、ミスマッチフレームの消費（取得後クリア）
    uint32_t ConsumeMismatch() {
        return _mismatchFrame.exchange(0, std::memory_order_acq_rel);
    }

    // ════════════════════════════════════════════════════
    // 状態クリア
    // ════════════════════════════════════════════════════

    void Reset() {
        std::memset(_ring, 0, sizeof(_ring));
        _writeHead.store(0, std::memory_order_relaxed);
        _mismatchFrame.store(0, std::memory_order_relaxed);
        _confirmedRemoteFrame.store(0, std::memory_order_relaxed);
        // _delay と _maxRollback はセッション固有パラメータなのでクリアしない
    }

    void InitializeConfirmedRemoteFrame(uint32_t frame) {
        _confirmedRemoteFrame.store(frame, std::memory_order_release);
    }

    static int32_t ToRelativeFrame(uint32_t absFrame, uint32_t baseFrame) {
        return static_cast<int32_t>(absFrame) - static_cast<int32_t>(baseFrame);
    }

private:
    MatchInputBuffer() = default;

    MatchFrameSlot _ring[RING_SIZE] = {};
    std::atomic<uint32_t> _writeHead{0};
    std::atomic<uint32_t> _mismatchFrame{0};
    std::atomic<uint32_t> _confirmedRemoteFrame{0};

    int16_t _delay       = 0;
    int16_t _maxRollback = 0;
};

} // namespace sync
} // namespace core
} // namespace cccaster
