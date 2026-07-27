#pragma once
// ============================================================================
// MatchInputBuffer — 入力のフレーム別バッファ
//
// 【責務】
//   フレーム番号をキーに、自入力（確定）と相手入力（予測 or 確定）を保持する。
//   予測が外れたフレームを記録し、ロールバックの起点として通知する。
//
// 【設計の前提】
//   スロットは frame % RING_SIZE に置く。周回すると古いフレームは上書きされる。
//   そのため読み書きの両方で「そのスロットが本当に要求フレームのものか」を
//   検証する。検証を省くと、600F 以上の進みが起きた瞬間に別フレームのデータを
//   黙って返す（旧実装の挙動）。
//
// 【デシンクの検出について】
//   冗長入力により同じフレームが何度も確定される。2回目以降で値が食い違うのは
//   通信の破綻かデシンクであり、黙って上書きしてはいけない。
//   ConfirmConflicts() で観測可能にしている。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>

namespace cccaster {
namespace core {
namespace sync {

struct MatchFrameSlot {
    uint32_t frame        = 0;      ///< このスロットが保持するフレーム番号
    bool     valid        = false;  ///< frame が意味を持つか（周回検証に使う）
    bool     rollbackable = false;  ///< ロールバック可能区域か
    uint32_t localInput   = 0;      ///< 自入力（確定）
    uint32_t remoteInput  = 0;      ///< 相手入力（予測 or 確定）
    bool     confirmed    = false;  ///< 相手入力が実パケットで確定済みか
};

class MatchInputBuffer {
public:
    static constexpr int RING_SIZE = 600;  // 10秒分 (60fps × 10s)

    static MatchInputBuffer& GetInstance() {
        static MatchInputBuffer instance;
        return instance;
    }

    // ════════════════════════════════════════════════════
    // 書き込み
    // ════════════════════════════════════════════════════

    /// 自入力を書き込む。相手入力は predicted で仮置きする。
    void WriteLocal(uint32_t frame, uint32_t localInput,
                    uint32_t predictedRemote, bool rollbackable) {
        Slot& s = _ring[frame % RING_SIZE];
        s.frame        = frame;
        s.valid        = true;
        s.rollbackable = rollbackable;
        s.localInput   = localInput;
        // 既に相手入力が確定済みのフレームなら、その値を保持する
        if (!(s.confirmed && s.frame == frame)) {
            s.remoteInput = predictedRemote;
            s.confirmed   = false;
        }
        _writeHead.store(frame, std::memory_order_release);
    }

    /// 相手入力を確定する。パケットは自入力の書き込みより先に届きうる。
    void ConfirmRemote(uint32_t frame, uint32_t input) {
        Slot& s = _ring[frame % RING_SIZE];

        const bool sameFrame = (s.valid && s.frame == frame);

        if (sameFrame && s.confirmed) {
            // 確定済みの値と食い違う = 冗長入力の不一致。黙って通してはいけない。
            if (s.remoteInput != input) {
                _confirmConflicts.fetch_add(1, std::memory_order_relaxed);
                RecordMismatch(frame);
            }
            return;  // 最初の確定値を正とする
        }

        if (sameFrame && s.remoteInput != input) {
            RecordMismatch(frame);   // 予測外れ
        }

        s.frame       = frame;
        s.valid       = true;
        s.remoteInput = input;
        s.confirmed   = true;

        const uint32_t prev = _confirmedRemoteFrame.load(std::memory_order_relaxed);
        if (!_hasConfirmedRemote.load(std::memory_order_relaxed) || frame > prev) {
            _confirmedRemoteFrame.store(frame, std::memory_order_release);
            _hasConfirmedRemote.store(true, std::memory_order_release);
        }
    }

    uint32_t GetWriteHead() const { return _writeHead.load(std::memory_order_acquire); }
    void     SetWriteHead(uint32_t frame) { _writeHead.store(frame, std::memory_order_release); }

    // ════════════════════════════════════════════════════
    // 読み出し
    // ════════════════════════════════════════════════════

    /// ゲームに書き込む入力を取り出す。
    /// @return 該当フレームが確定していなければ false（p1/p2 は変更しない）
    bool TryReadForGame(bool isHost, uint32_t& p1, uint32_t& p2) const {
        const uint32_t readPos = GetReadPos();
        const Slot& s = _ring[readPos % RING_SIZE];

        // 周回して別フレームのデータになっていないか必ず検証する
        if (!s.valid || s.frame != readPos || !s.confirmed) return false;

        if (isHost) { p1 = s.localInput;  p2 = s.remoteInput; }
        else        { p1 = s.remoteInput; p2 = s.localInput;  }
        return true;
    }

    /// 読み出し位置。writeHead から D+R だけ遡る。
    uint32_t GetReadPos() const {
        const uint32_t wh = _writeHead.load(std::memory_order_acquire);
        int32_t offset = static_cast<int32_t>(_delay) + static_cast<int32_t>(_maxRollback);
        if (offset < 1) offset = 1;  // 最低1フレーム遅延
        const int32_t pos = static_cast<int32_t>(wh) - offset;
        return (pos >= 0) ? static_cast<uint32_t>(pos) : 0;
    }

    /// 指定フレームのスロット。周回して別フレームなら nullptr。
    const MatchFrameSlot* FindSlot(uint32_t frame) const {
        const Slot& s = _ring[frame % RING_SIZE];
        return (s.valid && s.frame == frame) ? &s : nullptr;
    }

    bool     HasConfirmedRemote() const { return _hasConfirmedRemote.load(std::memory_order_acquire); }
    uint32_t GetConfirmedRemoteFrame() const { return _confirmedRemoteFrame.load(std::memory_order_acquire); }

    /// 相手の確定状況で頭打ちになる、実効的な進行可能フレーム。
    uint32_t GetEffectiveHead() const {
        const uint32_t wh = _writeHead.load(std::memory_order_acquire);
        int32_t offset = static_cast<int32_t>(_delay) + static_cast<int32_t>(_maxRollback);
        if (offset < 1) offset = 1;

        int32_t delayAdjusted = static_cast<int32_t>(wh) - offset;
        if (delayAdjusted < 0) delayAdjusted = 0;
        const uint32_t da = static_cast<uint32_t>(delayAdjusted);

        if (!_hasConfirmedRemote.load(std::memory_order_acquire)) return da;
        const uint32_t confirmed = _confirmedRemoteFrame.load(std::memory_order_acquire);
        return (da < confirmed) ? da : confirmed;
    }

    // ════════════════════════════════════════════════════
    // ミスマッチ（ロールバックの起点）
    // ════════════════════════════════════════════════════

    bool HasMismatch() const { return _hasMismatch.load(std::memory_order_acquire); }

    /// 最も古いミスマッチフレームを取り出してクリアする。
    /// @return 未発生なら false（frame は変更しない）
    bool ConsumeMismatch(uint32_t& frame) {
        if (!_hasMismatch.exchange(false, std::memory_order_acq_rel)) return false;
        frame = _mismatchFrame.load(std::memory_order_acquire);
        return true;
    }

    /// 確定済みフレームに異なる値が再確定された回数。0 以外ならデシンクを疑う。
    uint32_t ConfirmConflicts() const { return _confirmConflicts.load(std::memory_order_relaxed); }

    // ════════════════════════════════════════════════════
    // 初期化
    // ════════════════════════════════════════════════════

    void Initialize(uint32_t startFrame, int16_t delay, int16_t maxRollback) {
        Reset();
        SetWriteHead(startFrame);
        SetSyncParams(delay, maxRollback);
    }

    void SetSyncParams(int16_t delay, int16_t maxRollback) {
        _delay = delay;
        _maxRollback = maxRollback;
    }

    int16_t GetDelay() const { return _delay; }
    int16_t GetMaxRollback() const { return _maxRollback; }

    /// 進行状態を消す。D/R はセッション固有パラメータなので残す。
    void Reset() {
        std::memset(_ring, 0, sizeof(_ring));
        _writeHead.store(0, std::memory_order_relaxed);
        _mismatchFrame.store(0, std::memory_order_relaxed);
        _hasMismatch.store(false, std::memory_order_relaxed);
        _confirmedRemoteFrame.store(0, std::memory_order_relaxed);
        _hasConfirmedRemote.store(false, std::memory_order_relaxed);
        _confirmConflicts.store(0, std::memory_order_relaxed);
    }

private:
    using Slot = MatchFrameSlot;

    MatchInputBuffer() = default;

    void RecordMismatch(uint32_t frame) {
        if (!_hasMismatch.load(std::memory_order_relaxed)) {
            _mismatchFrame.store(frame, std::memory_order_release);
            _hasMismatch.store(true, std::memory_order_release);
            return;
        }
        // より古いミスマッチを優先する（ロールバックの起点になるため）
        if (frame < _mismatchFrame.load(std::memory_order_relaxed)) {
            _mismatchFrame.store(frame, std::memory_order_release);
        }
    }

    Slot _ring[RING_SIZE] = {};

    std::atomic<uint32_t> _writeHead{0};

    std::atomic<uint32_t> _mismatchFrame{0};
    std::atomic<bool>     _hasMismatch{false};

    std::atomic<uint32_t> _confirmedRemoteFrame{0};
    std::atomic<bool>     _hasConfirmedRemote{false};

    std::atomic<uint32_t> _confirmConflicts{0};

    int16_t _delay       = 0;
    int16_t _maxRollback = 0;
};

} // namespace sync
} // namespace core
} // namespace cccaster
