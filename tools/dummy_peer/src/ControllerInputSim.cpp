#include "ControllerInputSim.hpp"
#include "RealClock.hpp"
#include <cstdint>

namespace dummy_peer {

// ================================================================
// GenerateCharaSelectInput — キャラセレ入力シーケンス
//
// フレームシーケンス:
//   Phase 1 ( 0〜149F): Filter A 互換 — ニュートラル（Aボタン封印期間）
//   Phase 2 (150〜179F): 方向キー押し（カーソル移動）
//   Phase 3 (180〜213F): ニュートラル（Filter C の 3F ガード待ち余裕）
//   Phase 4 (214F〜):   A/Confirm 連打 → キャラ確定
// ================================================================
uint16_t ControllerInputSim::GenerateCharaSelectInput(uint32_t frameInPhase) {
    if (frameInPhase < 150) {
        // Phase 1: ニュートラル（Filter A 期間）
        return 0;
    }
    if (frameInPhase < 180) {
        // Phase 2: 右方向でカーソル移動
        // 上位8bit = 方向 (numpad 形式), 下位8bit = ボタン
        return static_cast<uint16_t>(DIR_RIGHT << 8);
    }
    if (frameInPhase < 214) {
        // Phase 3: ニュートラル（方向放し → Filter C の待機）
        return 0;
    }
    // Phase 4: 確定ボタン連打（A ボタン）
    // 偶数フレームのみ押す（連打）
    if ((frameInPhase % 2) == 0) {
        return BTN_A | BTN_CONFIRM;
    }
    return 0;
}

// ================================================================
// GenerateGameInput — InGame ランダム戦闘入力
//
// 簡易 LCG で方向+ボタンをランダムに生成する。
// seed に毎フレームのフレーム番号を渡すことで再現性を持たせる。
// ================================================================
uint32_t ControllerInputSim::GenerateGameInput(uint32_t seed) {
    _rng ^= seed;
    uint32_t r = NextRng();

    // 方向 (1〜9, numpad 形式)
    uint16_t dir = static_cast<uint16_t>((r % 9) + 1);

    // ボタン (A/B/C/D のランダム組み合わせ)
    uint16_t btn = static_cast<uint16_t>((NextRng() >> 16) & 0x000F);

    // START/FN1/FN2 は絶対に押さない（FilterBlockedButtons 互換）
    btn &= ~static_cast<uint16_t>(BTN_START);

    return (static_cast<uint32_t>(dir) << 16) | btn;
}

} // namespace dummy_peer
