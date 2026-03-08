#include "core_dll/mbaa_sync/rollback/RollbackEngine.hpp"
#include "core_dll/mbaa_sync/hooks/TimeHooks.hpp"
#include <algorithm>
#include <windows.h>

namespace cccaster::sync {

// QPC高精度タイマー (実時間ベース)
// 【重要】TimeHooks がゲーム用に QPC をフック (1000倍速等) しているため、
// 実時間での計測には RealQueryPerformanceCounter を使用する。
static int64_t QPCNowUs() {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    cccaster::core::hooks::TimeHooks::RealQueryPerformanceCounter(&now);
    return (now.QuadPart * 1000000LL) / freq.QuadPart;
}

// ============================================================================
// 初期化
// ============================================================================

void RollbackEngine::Initialize(int delayFrames, int maxRollback) {
    _delayFrames = delayFrames;
    _maxRollback = std::min(maxRollback, (int)StateRingBuffer::MAX_STATES);
    _currentFrame = 0;
    _lastConfirmedFrame = 0;
    _lastRollupTimeUs = 0;
    _lastRollbackDepth = 0;
    _isRollingBack = false;

    memset(_inputs, 0, sizeof(_inputs));
    memset(_sfxHistory, 0, sizeof(_sfxHistory));
    memset(_sfxFilterArray, 0, sizeof(_sfxFilterArray));

    _initialized = true;
    std::cout << "[RollbackEngine] Initialized: delay=" << delayFrames
              << " maxRollback=" << _maxRollback << "\n";
}

void RollbackEngine::SetupDumpEntries(const std::vector<DumpEntry>& entries) {
    _dumper.SetEntries(entries);
    _stateRing.Allocate(_dumper.GetTotalSize());
    std::cout << "[RollbackEngine] DumpEntries: " << _dumper.GetEntryCount()
              << " merged entries, " << _dumper.GetTotalSize() << " bytes/state\n";
}

// ============================================================================
// 入力予測
// ============================================================================

uint16_t RollbackEngine::PredictRemoteInput(uint32_t frameId) const {
    if (frameId == 0) return 0;
    return _inputs[(frameId - 1) % INPUT_BUFFER_SIZE].remote;
}

uint16_t RollbackEngine::GetResolvedRemoteInput(uint32_t frameId) const {
    const auto& entry = _inputs[frameId % INPUT_BUFFER_SIZE];
    if (entry.confirmed) return entry.remote;
    return PredictRemoteInput(frameId);
}

// ============================================================================
// 毎フレーム更新
// ============================================================================

bool RollbackEngine::UpdateFrame(uint16_t localInput) {
    if (!_initialized) return false;

    // 1. ローカル入力をバッファに保存（ディレイフレーム分未来）
    uint32_t inputFrame = _currentFrame + _delayFrames;
    auto& inputEntry = _inputs[inputFrame % INPUT_BUFFER_SIZE];
    inputEntry.local = localInput;

    // 2. 現在フレームの相手入力があるか確認
    auto& curEntry = _inputs[_currentFrame % INPUT_BUFFER_SIZE];

    if (!curEntry.confirmed) {
        // まだ届いていない → スタール判定
        uint32_t unconfirmedDepth = _currentFrame - _lastConfirmedFrame;
        if (unconfirmedDepth >= static_cast<uint32_t>(_maxRollback)) {
            // 予測限界を超えた → 待機
            return false;
        }

        // 推測入力で投機的実行
        curEntry.remote = PredictRemoteInput(_currentFrame);
    }

    // 3. StateをSave（予測フレームのみ）
    if (!curEntry.confirmed) {
        _stateRing.Save(_currentFrame, _dumper);
    }

    // 4. 入力をゲームメモリに書き込み
    // (実際の書き込みはnet_Versus_main側で行う — ここでは保存のみ)

    _currentFrame++;
    return true;
}

// ============================================================================
// リモート入力受信
// ============================================================================

void RollbackEngine::OnRemoteInputReceived(uint32_t frameId, uint16_t remoteInput) {
    auto& entry = _inputs[frameId % INPUT_BUFFER_SIZE];

    // 過去の予測と答え合わせ
    bool wasPredicted = !entry.confirmed;
    uint16_t predictedInput = entry.remote;

    // 確定入力を保存
    entry.remote = remoteInput;
    entry.confirmed = true;

    // 確定フレームを前進
    while (_inputs[(_lastConfirmedFrame + 1) % INPUT_BUFFER_SIZE].confirmed
           && _lastConfirmedFrame + 1 <= _currentFrame) {
        _lastConfirmedFrame++;
    }

    // ロールバック判定: 予測が間違っていた + 既に通過したフレーム
    if (wasPredicted && predictedInput != remoteInput && frameId < _currentFrame) {
        std::cout << "[Rollback] Triggered at F" << frameId
                  << " (current=" << _currentFrame
                  << " depth=" << (_currentFrame - frameId) << ")\n";
        DoRollback(frameId);
    }
}

// ============================================================================
// ロールバック＆ロールアップ
// ============================================================================

void RollbackEngine::DoRollback(uint32_t mismatchFrame) {
    _lastRollbackDepth = _currentFrame - mismatchFrame;
    _rerunStartTimeUs = 0; // ProcessRerunFrame初回で設定

    // 1. LoadState（過去へ復元）
    if (!_stateRing.Load(mismatchFrame, _dumper)) {
        std::cerr << "[Rollback] FATAL: LoadState failed at F" << mismatchFrame << "\n";
        return;
    }

    // 2. リプレイ補正
    FixReplayPointers(_lastRollbackDepth);

    // 3. SFXフィルタ初期化
    InitSfxFilter(mismatchFrame, _currentFrame);

    // 4. フラグ設定 → 次フレーム以降 ProcessRerunFrame() で処理
    _isRollingBack = true;
    _rerunFrame = mismatchFrame;
    _rerunTargetFrame = _currentFrame;

    std::cout << "[Rollback] Started: mismatch=F" << mismatchFrame
              << " target=F" << _currentFrame
              << " depth=" << _lastRollbackDepth << "\n";
    // ★ forループなし。ゲーム本体に制御を返す。
    //   高速化ON(SetFastForward)はProcessInGameSync側で行う
}

// ============================================================================
// 協調型ロールアップ: 1フレーム分のrerun
// ============================================================================

bool RollbackEngine::ProcessRerunFrame(uint16_t& outP1, uint16_t& outP2) {
    // 初回呼び出し時に計測開始（DoRollback→Adaptive_FrameSleep間の時間を除外）
    if (_rerunStartTimeUs == 0) {
        _rerunStartTimeUs = QPCNowUs();
    }
    auto& entry = _inputs[_rerunFrame % INPUT_BUFFER_SIZE];
    outP1 = entry.local;
    outP2 = GetResolvedRemoteInput(_rerunFrame);

    // SFX履歴更新
    SaveRerunSounds(_rerunFrame);

    // 未確定フレームはStateを保存（再ロールバック用）
    if (!entry.confirmed) {
        _stateRing.Save(_rerunFrame, _dumper);
    }

    _rerunFrame++;

    // rerun完了判定
    if (_rerunFrame >= _rerunTargetFrame) {
        FinishedRerunSounds();
        _isRollingBack = false;
        _lastRollupTimeUs = QPCNowUs() - _rerunStartTimeUs;

        std::cout << "[Rollback] Complete: depth=" << _lastRollbackDepth
                  << " time=" << _lastRollupTimeUs << "us\n";
        return true;  // 完了
    }
    return false; // 継続
}

// ============================================================================
// SFXフィルタ（レガシー DllRollbackManager 準拠）
// ============================================================================

void RollbackEngine::InitSfxFilter(uint32_t rollbackFrame, uint32_t currentFrame) {
    // ロールバック範囲(R, S)のSFX履歴をORマージ
    memset(_sfxFilterArray, 0, CC_SFX_ARRAY_LEN);

    for (uint32_t i = rollbackFrame + 1; i < currentFrame; ++i) {
        const uint8_t* history = _sfxHistory[i % NUM_ROLLBACK_STATES];
        for (uint32_t j = 0; j < CC_SFX_ARRAY_LEN; ++j) {
            _sfxFilterArray[j] |= history[j];
        }
    }

    // 0x80フラグ設定（再生済みフラグ）
    for (uint32_t j = 0; j < CC_SFX_ARRAY_LEN; ++j) {
        if (_sfxFilterArray[j])
            _sfxFilterArray[j] = 0x80;
    }
}

void RollbackEngine::SaveRerunSounds(uint32_t frame) {
    uint8_t* history = _sfxHistory[frame % NUM_ROLLBACK_STATES];

    for (uint32_t j = 0; j < CC_SFX_ARRAY_LEN; ++j) {
        if (_sfxFilterArray[j] & ~0x80)
            history[j] = 1;
        else
            history[j] = 0;
    }
}

void RollbackEngine::FinishedRerunSounds() {
    // ロールバック後にSFXが鳴らなかった場合、ミュートで再生してキャンセル
    for (uint32_t j = 0; j < CC_SFX_ARRAY_LEN; ++j) {
        if (_sfxFilterArray[j] == 0x80) {
            CC_SFX_ARRAY_ADDR[j] = 1;
            // sfxMuteArrayへの書き込みはAsmHacks経由（後で接続）
        }
    }

    memset(_sfxFilterArray, 0, CC_SFX_ARRAY_LEN);
}

// ============================================================================
// リプレイポインタ補正（レガシー準拠）
// ============================================================================

void RollbackEngine::FixReplayPointers(int rollbackFrames) {
    for (int rb = rollbackFrames; rb > 0; --rb) {
        RepRound** tblEndPtr = reinterpret_cast<RepRound**>(CC_REPROUND_TBL_ENDPTR_ADDR);
        if (!*tblEndPtr) break;

        RepRound* curRound = (*tblEndPtr - 1);
        if (!curRound->inputs) break;

        // 4プレイヤー分のリプレイ入力インデックスを補正
        for (int i = 0; i < 4; ++i) {
            RepInputContainer* inputs = &(curRound->inputs[i]);
            if (!inputs->states) continue;

            RepInputState* state = &(inputs->states[inputs->activeIndex]);
            if (!state->frameCount) continue;

            if (state->frameCount == 1) {
                memset(state, 0, sizeof(RepInputState));
                inputs->statesEnd -= sizeof(RepInputState);
                inputs->activeIndex--;
            } else {
                state->frameCount--;
            }
        }
    }
}

} // namespace cccaster::sync
