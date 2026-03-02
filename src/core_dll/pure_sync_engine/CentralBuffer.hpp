#pragma once
// ============================================================================
// CentralBuffer — 全フレーム入力管理（Central Ring Buffer）
//
// 【責務】
//   自入力・相手入力をフレームごとにセットで管理する巨大リングバッファ。
//   通信スレッド(SyncCoordinator)が書込み、ゲームスレッド(SceneRunner)が読取り。
//
// 【スレッド安全性】
//   - Write系: 通信スレッドのみ（単一writer、mutex不要）
//   - Read系 / PlayHead: ゲームスレッドから読取り（atomic同期）
//   - CheckMismatch: ゲームスレッドのみ
//
// 【観戦者対応】
//   将来的に確定済み入力を線形バッファに蓄積し、
//   観戦者へのストリーミング配信に使用する。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>

namespace cccaster {
namespace core {
namespace sync {

// ── フレームスロット ──
struct FrameSlot {
    uint32_t localInput      = 0;  // 自入力（確定済み）
    uint32_t remoteInput     = 0;  // 相手入力（受信 or 予測コピー）
    uint32_t localBaseFrame  = 0;  // 書込み時の自分のフレーム
    uint32_t remoteBaseFrame = 0;  // パケットから取得した相手のフレーム
    bool     remoteConfirmed = false; // 相手入力が実パケットで確定済みか
};

class CentralBuffer {
public:
    static constexpr int RING_SIZE = 600; // 10秒分 (60fps × 10s)

    // ── シングルトン ──
    static CentralBuffer& GetInstance() {
        static CentralBuffer instance;
        return instance;
    }

    // ────────────────────────────────────────────────
    // 通信スレッドから呼ばれる
    // ────────────────────────────────────────────────

    /// ローカル入力を書込み（subTick0 でフレーム確定時）
    void WriteLocalInput(uint32_t frame, uint32_t input) {
        auto& slot = _ring[frame % RING_SIZE];
        slot.localInput     = input;
        slot.localBaseFrame = frame;
        _writeHead.store(frame, std::memory_order_release);
    }

    /// リモート入力を書込み（GAME_TICK 受信時）
    /// @param frame      相手パケットに載っていた baseFrame
    /// @param input      相手の入力ビットマスク
    /// @param peerFrame  相手のフレーム番号（= frame と同じ）
    void WriteRemoteInput(uint32_t frame, uint32_t input, uint32_t peerFrame) {
        auto& slot = _ring[frame % RING_SIZE];

        // 予測入力と異なる確定入力が来た場合を検出するため、
        // 上書き前の値を記録
        uint32_t prevRemote = slot.remoteInput;
        bool     wasConfirmed = slot.remoteConfirmed;

        slot.remoteInput     = input;
        slot.remoteBaseFrame = peerFrame;
        slot.remoteConfirmed = true;

        // 予測と異なる確定入力が来た → ミスマッチフレームを更新
        if (!wasConfirmed && prevRemote != input) {
            // 最も古いミスマッチフレームを記録
            uint32_t current = _mismatchFrame.load(std::memory_order_relaxed);
            if (current == 0 || frame < current) {
                _mismatchFrame.store(frame, std::memory_order_release);
            }
        }

        // 最新のリモートベースフレームを更新
        uint32_t currentLatest = _latestRemoteBaseFrame.load(std::memory_order_relaxed);
        if (peerFrame > currentLatest) {
            _latestRemoteBaseFrame.store(peerFrame, std::memory_order_release);
        }
    }

    /// 未受信フレームの予測入力を書込み（直前の入力をコピー）
    void WritePredictedRemoteInput(uint32_t frame, uint32_t prevInput) {
        auto& slot = _ring[frame % RING_SIZE];
        if (!slot.remoteConfirmed) {
            slot.remoteInput     = prevInput;
            slot.remoteBaseFrame = frame;
            slot.remoteConfirmed = false;
        }
    }

    // ────────────────────────────────────────────────
    // ゲームスレッドから呼ばれる
    // ────────────────────────────────────────────────

    /// 指定フレームのスロットを取得（読取り専用）
    const FrameSlot& GetSlot(uint32_t frame) const {
        return _ring[frame % RING_SIZE];
    }

    /// 最新のリモートベースフレーム（相手がどこまで進んでいるか）
    uint32_t GetLatestRemoteBaseFrame() const {
        return _latestRemoteBaseFrame.load(std::memory_order_acquire);
    }

    /// 再生ヘッド（ゲームスレッドが再生中のフレーム）
    uint32_t GetPlayHead() const {
        return _playHead.load(std::memory_order_acquire);
    }

    /// 再生ヘッドを進める
    void AdvancePlayHead() {
        _playHead.fetch_add(1, std::memory_order_release);
    }

    /// 再生ヘッドを指定フレームに設定（Catch-up 用）
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
        _latestRemoteBaseFrame.store(0, std::memory_order_relaxed);
        _mismatchFrame.store(0, std::memory_order_relaxed);
    }

private:
    CentralBuffer() = default;

    FrameSlot _ring[RING_SIZE] = {};
    std::atomic<uint32_t> _writeHead{0};              // 通信スレッド書込み位置
    std::atomic<uint32_t> _playHead{0};               // ゲームスレッド再生位置
    std::atomic<uint32_t> _latestRemoteBaseFrame{0};  // 相手の最新フレーム
    std::atomic<uint32_t> _mismatchFrame{0};          // 予測外れフレーム (0=なし)
};

} // namespace sync
} // namespace core
} // namespace cccaster
