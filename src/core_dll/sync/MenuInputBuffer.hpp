#pragma once
// ============================================================================
// MenuInputBuffer — キャラクターセレクト用入力管理バッファ
//
// 【責務】
//   CharaSelect フェーズなど、ロールバックが発生しないUI操作の入力を同期するためのバッファ。
//   単純な遅延付き FIFO として振る舞い、予測外れの検知や再実行制御は持たない。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>

namespace cccaster {
namespace core {
namespace sync {

// ── メニュー用フレームスロット ──
struct MenuFrameSlot {
    uint32_t  frame        = 0;      // 相対フレーム番号
    uint32_t  localInput   = 0;      // 自入力（確定）
    uint32_t  remoteInput  = 0;      // 相手入力（確定または直前コピー）
    bool      confirmed    = false;  // 相手入力が実パケットで確定済みか
};

class MenuInputBuffer {
public:
    static constexpr int RING_SIZE = 600; // 10秒分 (60fps × 10s)

    // ── シングルトン ──
    static MenuInputBuffer& GetInstance() {
        static MenuInputBuffer instance;
        return instance;
    }

    // ════════════════════════════════════════════════════
    // Write系
    // ════════════════════════════════════════════════════

    void WriteSlot(uint32_t frame, uint32_t localInput, uint32_t remoteInput, bool confirmed) {
        auto& slot = _ring[frame % RING_SIZE];
        slot.frame        = frame;
        slot.localInput   = localInput;
        slot.remoteInput  = remoteInput;
        slot.confirmed    = confirmed;
    }

    void ConfirmRemote(uint32_t frame, uint32_t input) {
        auto& slot = _ring[frame % RING_SIZE];

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

    void Initialize(uint32_t startFrame, int16_t delay) {
        Reset();
        SetWriteHead(startFrame);
        InitializeConfirmedRemoteFrame(startFrame);
        SetDelay(delay);
    }

    void SetDelay(int16_t delay) {
        _delay = delay;
    }

    int16_t GetDelay() const { return _delay; }

    // ════════════════════════════════════════════════════
    // Read系
    // ════════════════════════════════════════════════════

    bool ReadFrameForGame(bool isHost, uint32_t& p1, uint32_t& p2) const {
        uint32_t readPos = GetReadPos();
        if (readPos == 0) return false;
        const auto& slot = _ring[readPos % RING_SIZE];
        if (!slot.confirmed) return false;

        if (isHost) { p1 = slot.localInput; p2 = slot.remoteInput; }
        else        { p1 = slot.remoteInput; p2 = slot.localInput; }
        return true;
    }

    uint32_t GetReadPos() const {
        uint32_t wh = _writeHead.load(std::memory_order_acquire);
        int32_t offset = static_cast<int32_t>(_delay);
        if (offset < 1) offset = 1;  // 最低1フレーム遅延
        int32_t pos = static_cast<int32_t>(wh) - offset;
        return (pos >= 0) ? static_cast<uint32_t>(pos) : 0;
    }

    uint32_t GetConfirmedRemoteFrame() const {
        return _confirmedRemoteFrame.load(std::memory_order_acquire);
    }

    uint32_t GetEffectiveHead() const {
        uint32_t wh = _writeHead.load(std::memory_order_acquire);
        int32_t offset = static_cast<int32_t>(_delay);
        if (offset < 1) offset = 1;
        int32_t delayAdjusted = static_cast<int32_t>(wh) - offset;
        if (delayAdjusted < 0) delayAdjusted = 0;

        uint32_t confirmed = _confirmedRemoteFrame.load(std::memory_order_acquire);
        uint32_t da = static_cast<uint32_t>(delayAdjusted);

        if (confirmed == 0) return da;
        return (da < confirmed) ? da : confirmed;
    }

    const MenuFrameSlot& GetSlot(uint32_t frame) const {
        return _ring[frame % RING_SIZE];
    }

    // ════════════════════════════════════════════════════
    // 状態クリア
    // ════════════════════════════════════════════════════

    void Reset() {
        std::memset(_ring, 0, sizeof(_ring));
        _writeHead.store(0, std::memory_order_relaxed);
        _confirmedRemoteFrame.store(0, std::memory_order_relaxed);
        _delay = 0;
    }

    void InitializeConfirmedRemoteFrame(uint32_t frame) {
        _confirmedRemoteFrame.store(frame, std::memory_order_release);
    }

    static int32_t ToRelativeFrame(uint32_t absFrame, uint32_t baseFrame) {
        return static_cast<int32_t>(absFrame) - static_cast<int32_t>(baseFrame);
    }

private:
    MenuInputBuffer() = default;

    MenuFrameSlot _ring[RING_SIZE] = {};
    std::atomic<uint32_t> _writeHead{0};
    std::atomic<uint32_t> _confirmedRemoteFrame{0};

    int16_t _delay = 0;
};

} // namespace sync
} // namespace core
} // namespace cccaster
