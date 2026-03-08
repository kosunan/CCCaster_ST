#pragma once
// ============================================================================
// FrameInputBuffer — 全フレーム入力管理（Central Ring Buffer）
//
// 【責務】
//   自入力・相手入力をフレームごとにセットで管理する巨大リングバッファ。
//   DLLスレッドが CommitFrame() で入力蓄積、ゲームスレッドが読取り。
//
// 【スレッド安全性】
//   - WriteSlot / ConfirmRemote / SetWriteHead: 通信スレッドのみ（単一writer）
//   - GetSlot / GetWriteHead / GetReadPos: ゲームスレッドから読取り（atomic同期）
//   - ConsumeMismatch: ゲームスレッドのみ
//
// 【データフロー】
//   DLLスレッド  → CommitFrame() → writeHead 更新
//   DLLスレッド  ← GetReadPos() → GetSlot(readPos) → WriteInput
//   ioスレッド   → ConfirmRemote() (受信コールバック)
//
// 【readPos 算出方式】
//   readPos = writeHead - delay - maxRollback
//   非ロールバック区間: confirmed=true のスロットのみ消費（未確定なら待つ）
//   ロールバック区間:   confirmed=false でも予測入力で進行可（後からロールバック）
//
// 【注意】
//   CC_SKIP_FRAMES_ADDR は使用禁止。描画制御は API hook (RenderSkip) で行う。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>

// CommitFrame() 内部で使用する依存ヘッダ（前方宣言では不十分）
#include "core_dll/mbaa_game/monitor/GamePhaseDetector.hpp"
#include "core_dll/mbaa_sync/common/DirectInputHook.hpp"

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

class FrameInputBuffer {
public:
    static constexpr int RING_SIZE = 600; // 10秒分 (60fps × 10s)

    // ── シングルトン ──
    static FrameInputBuffer& GetInstance() {
        static FrameInputBuffer instance;
        return instance;
    }

    // ════════════════════════════════════════════════════
    // DLLスレッドから呼ばれる (Write系)
    // @thread_safety DLLスレッド専用（単一writer）
    // ════════════════════════════════════════════════════

    /// @brief 引数なし CommitFrame — 内部で全情報を収集してスロット書込み
    /// @details (1) Phase取得 (2) 入力Poll (3) rollbackable判定 (4) スロット書込み + writeHead更新
    /// @thread_safety DLLスレッド専用
    void CommitFrame() {
        // (1) 現在の画面フェーズ
        auto phase = cccaster::game_interface::GameMonitor::GetCurrentPhase();
        uint8_t phaseU8 = static_cast<uint8_t>(phase);

        // ゲームが入力を受け付けるフェーズのみCBに書込み
        // CharaSelect(2), InGame(4), Rematch(5) → 有効
        // Unknown(0), Title(1), Loading(3)      → スキップ（ゲームが廃棄する入力）
        using GP = cccaster::game_interface::GamePhase;
        if (phase != GP::CharaSelect && phase != GP::InGame && phase != GP::Rematch) {
            return;  // 入力無効フェーズ — CB に書込まない
        }

        uint32_t frame = _writeHead.load(std::memory_order_relaxed) + 1;

        // (2) ローカル入力読取
        cccaster::game_interface::DirectInputHook::Poll();
        uint32_t localInput = _isHost
            ? cccaster::game_interface::DirectInputHook::GetPlayer1Input()
            : cccaster::game_interface::DirectInputHook::GetPlayer2Input();

        // (3) rollbackable判定 (InGame のみ)
        bool rb = (phase == GP::InGame);

        // (4) スロット書込み + writeHead 更新
        auto& slot = _ring[frame % RING_SIZE];
        slot.frame        = frame;
        slot.gamePhase    = phaseU8;
        slot.rollbackable = rb;
        slot.localInput   = localInput;
        slot.remoteInput  = 0;      // 未確定
        slot.confirmed    = false;
        _writeHead.store(frame, std::memory_order_release);
    }

    /// @brief 旧API: 引数付き CommitFrame (段階的移行用)
    void CommitFrame(uint32_t frame, uint8_t gamePhase, bool rollbackable,
                     uint32_t localInput, uint32_t remoteInput, bool confirmed) {
        WriteSlot(frame, gamePhase, rollbackable, localInput, remoteInput, confirmed);
        SetWriteHead(frame);
    }

    /// @brief スロット書込み（低レベルAPI — 通常は CommitFrame を使うこと）
    /// @thread_safety 通信スレッド専用
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

    /// @brief 相手入力を確定更新（GAME_TICK 受信時）
    /// @details 予測と異なる確定入力が来た場合、mismatchFrame を記録する。
    /// @thread_safety 通信スレッド専用
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

        // 確定済みリモートフレーム最大値を追跡
        uint32_t prev = _confirmedRemoteFrame.load(std::memory_order_relaxed);
        if (frame > prev) {
            _confirmedRemoteFrame.store(frame, std::memory_order_release);
        }
    }

    /// @brief writeHead（最新フレーム番号）を取得
    /// @thread_safety どのスレッドからでも読取り可
    uint32_t GetWriteHead() const {
        return _writeHead.load(std::memory_order_acquire);
    }

    /// @brief writeHead を設定（低レベルAPI — 通常は CommitFrame を使うこと）
    /// @thread_safety 通信スレッド専用
    void SetWriteHead(uint32_t frame) {
        _writeHead.store(frame, std::memory_order_release);
    }

    // ════════════════════════════════════════════════════
    // 初期化・パラメータ設定
    // ════════════════════════════════════════════════════

    /// @brief 全状態リセット＋初期フレーム＋同期パラメータを一括設定（推奨）
    /// @thread_safety Start前のシングルスレッド状態で呼ぶこと
    void Initialize(uint32_t startFrame, int16_t delay, int16_t maxRollback, bool isHost = false) {
        Reset();
        _isHost = isHost;
        SetWriteHead(startFrame);
        InitializeConfirmedRemoteFrame(startFrame);
        SetSyncParams(delay, maxRollback);
    }

    /// @brief ディレイ + 最大ロールバック を設定
    /// @thread_safety UIスレッドからも呼ばれる（動的変更時）
    void SetSyncParams(int16_t delay, int16_t maxRollback) {
        _delay = delay;
        _maxRollback = maxRollback;
    }

    int16_t GetDelay() const { return _delay; }
    int16_t GetMaxRollback() const { return _maxRollback; }

    // ════════════════════════════════════════════════════
    // ゲームスレッドから呼ばれる (Read系)
    // @thread_safety ゲームスレッドから読取り（atomic同期）
    //
    // 【Read API 使い分けガイド】
    //   GetReadPos()        — ディレイ/ロールバック補正のみ。単純な読取位置。
    //   GetEffectiveHead()  — GetReadPos + confirmedRemoteFrame の min。
    //                         SleepFrame の gap 計算に使う（ゲーム進行可能フレーム）。
    //   ReadFrameForGame()  — 入力読取の完全パイプライン（推奨）。
    //                         GetReadPos → confirmed チェック → P1/P2振分け。
    // ════════════════════════════════════════════════════

    /// @brief ゲーム入力読取の完全パイプライン（推奨API）
    /// @details GetReadPos → confirmed チェック → isHost に応じた P1/P2 振分けを一括実行。
    /// @param isHost true=ホスト側（localInput→P1, remoteInput→P2）
    /// @param[out] p1 P1側入力
    /// @param[out] p2 P2側入力
    /// @return 読取り成功なら true（readPos有効 かつ confirmed）
    /// @thread_safety ゲームスレッド専用
    bool ReadFrameForGame(bool isHost, uint32_t& p1, uint32_t& p2) const {
        uint32_t readPos = GetReadPos();
        if (readPos == 0) return false;
        const auto& slot = _ring[readPos % RING_SIZE];
        if (!slot.confirmed) return false;
        if (isHost) { p1 = slot.localInput; p2 = slot.remoteInput; }
        else        { p1 = slot.remoteInput; p2 = slot.localInput; }
        return true;
    }

    /// @brief DLL の読取位置を算出: writeHead - max(delay + maxRollback, 1)
    /// @details 最低1フレームのオフセットを保証（D=0,R=0でもリモート未確定フレーム読取を防止）
    /// @return 読み取るべきフレーム番号（0 以下にはならない）
    /// @thread_safety ゲームスレッドから読取り可
    uint32_t GetReadPos() const {
        uint32_t wh = _writeHead.load(std::memory_order_acquire);
        int32_t offset = static_cast<int32_t>(_delay) + static_cast<int32_t>(_maxRollback);
        if (offset < 1) offset = 1;  // 最低1フレーム遅延を保証
        int32_t pos = static_cast<int32_t>(wh) - offset;
        return (pos >= 0) ? static_cast<uint32_t>(pos) : 0;
    }

    /// @brief リモート入力が確定している最新フレーム
    /// @thread_safety どのスレッドからでも読取り可
    uint32_t GetConfirmedRemoteFrame() const {
        return _confirmedRemoteFrame.load(std::memory_order_acquire);
    }

    /// @brief ゲームが進行可能な実効フレーム: min(writeHead-(D+R), confirmedRemoteFrame)
    /// @details confirmedRemoteFrame が未初期化(0)の場合は delayAdjusted のみを返す
    /// @thread_safety ゲームスレッドから読取り可
    uint32_t GetEffectiveHead() const {
        uint32_t wh = _writeHead.load(std::memory_order_acquire);
        int32_t offset = static_cast<int32_t>(_delay) + static_cast<int32_t>(_maxRollback);
        if (offset < 1) offset = 1;  // 最低1Fオフセット
        int32_t delayAdjusted = static_cast<int32_t>(wh) - offset;
        if (delayAdjusted < 0) delayAdjusted = 0;

        uint32_t confirmed = _confirmedRemoteFrame.load(std::memory_order_acquire);
        uint32_t da = static_cast<uint32_t>(delayAdjusted);

        // confirmedRemoteFrame がまだ未初期化（0）の場合は
        // delayAdjusted のみで制御（起動初期のフリーズを防止）
        if (confirmed == 0) return da;

        return (da < confirmed) ? da : confirmed;
    }

    /// @brief 指定フレームのスロットを取得（読取り専用・低レベルAPI）
    /// @thread_safety ゲームスレッドから読取り可
    const FrameSlot& GetSlot(uint32_t frame) const {
        return _ring[frame % RING_SIZE];
    }

    /// @brief ロールバック判定: 予測外れが発生したフレームを返す (0=なし)
    /// @details 取得後にクリアされる（consume セマンティクス）
    /// @thread_safety ゲームスレッド専用
    uint32_t ConsumeMismatch() {
        return _mismatchFrame.exchange(0, std::memory_order_acq_rel);
    }

    /// @brief 全状態リセット（低レベルAPI — 通常は Initialize を使うこと）
    /// @thread_safety シングルスレッド状態で呼ぶこと
    void Reset() {
        std::memset(_ring, 0, sizeof(_ring));
        _writeHead.store(0, std::memory_order_relaxed);
        _mismatchFrame.store(0, std::memory_order_relaxed);
        _confirmedRemoteFrame.store(0, std::memory_order_relaxed);
        _delay = 0;
        _maxRollback = 0;
    }

    /// @brief confirmedRemoteFrame の初期値を設定
    /// @thread_safety シングルスレッド状態で呼ぶこと
    void InitializeConfirmedRemoteFrame(uint32_t frame) {
        _confirmedRemoteFrame.store(frame, std::memory_order_release);
    }

private:
    FrameInputBuffer() = default;

    FrameSlot _ring[RING_SIZE] = {};
    std::atomic<uint32_t> _writeHead{0};       // 通信スレッド書込み位置（= 旧 currentFrame）
    std::atomic<uint32_t> _mismatchFrame{0};   // 予測外れフレーム (0=なし)
    std::atomic<uint32_t> _confirmedRemoteFrame{0}; // リモート入力確定済み最新フレーム

    // 同期パラメータ（NetplaySession::Start で設定、以後不変）
    int16_t _delay       = 0;
    int16_t _maxRollback = 0;

    // ロール識別（CommitFrame で P1/P2 どちらの入力を読むか）
    bool _isHost = false;
};

} // namespace sync
} // namespace core
} // namespace cccaster
