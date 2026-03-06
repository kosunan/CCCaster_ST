#pragma once
// ============================================================================
// RollbackEngine — ロールバック＆ロールアップ管理
// 処理速度最優先: キー確定＋ゲーム進行のみ
// ============================================================================

#include <cstdint>
#include "core_dll/game_memory_accessor/dump/MemDumper.hpp"
#include "core_dll/game_memory_accessor/state/StateRingBuffer.hpp"
#include "core_dll/game_memory_accessor/MbaaConstants.hpp"

namespace cccaster::sync {

// リプレイ構造体（レガシー DllRollbackManager.hpp 準拠）
#pragma pack(push, 1)
struct RepInputState {
    char unk1;
    char frameCount;
    char unk2[6];
};

struct RepInputContainer {
    char           unk1[4];
    RepInputState* states;
    char*          statesEnd;
    char           unk2[4];
    int            totalFrameCount;
    int            totalFrameCount2;
    int            activeIndex;
    char           unk3[4];
};

struct RepRound {
    char               unk1[0x120];
    RepInputContainer* inputs;
    char               unk2[0x1C];
};
#pragma pack(pop)

class RollbackEngine {
public:
    // ===== 入力エントリ =====
    struct InputEntry {
        uint16_t local    = 0;
        uint16_t remote   = 0;
        bool     confirmed = false; // 相手入力が確定済みか
    };

    // 初期化（キャラセレ時に呼ぶ）
    void Initialize(int delayFrames, int maxRollback);

    // メモリダンプエントリの設定（ゲーム開始前に呼ぶ）
    void SetupDumpEntries(const std::vector<DumpEntry>& entries);

    // 毎フレーム呼び出し
    // @return true=描画OK, false=スタール（待機）
    bool UpdateFrame(uint16_t localInput);

    // リモート入力受信時（メインスレッドから呼ぶ）
    void OnRemoteInputReceived(uint32_t frameId, uint16_t remoteInput);

    // 協調型ロールアップ: 1フレーム分のrerun処理
    // @param outP1 [out] このフレームのP1入力
    // @param outP2 [out] このフレームのP2入力
    // @return true = rerun完了（最終フレーム到達）
    bool ProcessRerunFrame(uint16_t& outP1, uint16_t& outP2);

    // === アクセサ ===
    uint32_t GetCurrentFrame() const { return _currentFrame; }
    uint32_t GetLastConfirmedFrame() const { return _lastConfirmedFrame; }
    int GetDelayFrames() const { return _delayFrames; }
    int GetMaxRollback() const { return _maxRollback; }
    int64_t GetLastRollupTimeUs() const { return _lastRollupTimeUs; }
    int GetLastRollbackDepth() const { return _lastRollbackDepth; }
    bool IsRollingBack() const { return _isRollingBack; }

private:
    // ロールバック実行
    void DoRollback(uint32_t mismatchFrame);

    // 推測入力（直前フレームのコピー）
    uint16_t PredictRemoteInput(uint32_t frameId) const;

    // 解決済みリモート入力を取得（確定 or 予測）
    uint16_t GetResolvedRemoteInput(uint32_t frameId) const;

    // SFXフィルタ操作
    void InitSfxFilter(uint32_t rollbackFrame, uint32_t currentFrame);
    void SaveRerunSounds(uint32_t frame);
    void FinishedRerunSounds();

    // リプレイポインタ補正
    void FixReplayPointers(int rollbackFrames);

    // === メモリダンプ ===
    MemDumper _dumper;
    StateRingBuffer _stateRing;

    // === 入力バッファ（256エントリ固定リングバッファ） ===
    static constexpr int INPUT_BUFFER_SIZE = 256;
    InputEntry _inputs[INPUT_BUFFER_SIZE] = {};

    // === SFXフィルタ ===
    uint8_t _sfxHistory[NUM_ROLLBACK_STATES][CC_SFX_ARRAY_LEN] = {};
    uint8_t _sfxFilterArray[CC_SFX_ARRAY_LEN] = {};

    // === フレーム管理 ===
    uint32_t _currentFrame = 0;
    uint32_t _lastConfirmedFrame = 0;
    int _delayFrames = 0;
    int _maxRollback = 0;

    // === パフォーマンス計測 ===
    int64_t _lastRollupTimeUs = 0;
    int _lastRollbackDepth = 0;
    bool _isRollingBack = false;
    bool _initialized = false;

    // === 協調型ロールアップ状態 ===
    uint32_t _rerunFrame = 0;        // 現在のrerunフレーム
    uint32_t _rerunTargetFrame = 0;  // rerun到達目標
    int64_t  _rerunStartTimeUs = 0;  // rerun開始時刻(計測用)
};

} // namespace cccaster::sync
