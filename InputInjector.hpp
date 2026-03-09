#pragma once
#include <cstdint>

// ==========================================================
// MBAA メモリアドレス定義 (ユーザー特定)
// ==========================================================

// 方向キーアドレス (0x02, 0x04, 0x06, 0x08)
constexpr uintptr_t MBAA_P1_DIR_ADDR = 0x0055541B;

// ボタン(A,B,C,D)のPush/Hold合成アドレス
// 上位4ビット(0x10〜0x80)がHold、下位4ビット(0x01〜0x08)がPushとして同居する1バイト
constexpr uintptr_t MBAA_P1_BTN_ADDR = 0x0055541D;

// Eキー別枠ホールドアドレス (0x01)
constexpr uintptr_t MBAA_P1_E_ADDR   = 0x0055541E;


// ==========================================================
// 16進数 フラグ定義群
// ==========================================================
namespace MbaaDirFlag {
    constexpr uint8_t NONE  = 0x00;
    constexpr uint8_t DOWN  = 0x02;
    constexpr uint8_t LEFT  = 0x04;
    constexpr uint8_t RIGHT = 0x06;
    constexpr uint8_t UP    = 0x08;
}

namespace MbaaHoldFlag {
    constexpr uint8_t NONE = 0x00;
    constexpr uint8_t A    = 0x10;
    constexpr uint8_t B    = 0x20;
    constexpr uint8_t C    = 0x40;
    constexpr uint8_t D    = 0x80;
    constexpr uint8_t ABC  = 0x70; // Eニュートラル用
    constexpr uint8_t AD   = 0x90; // E横用
    constexpr uint8_t AB   = 0x30; // E下用
}

namespace MbaaPushFlag {
    constexpr uint8_t NONE = 0x00;
    constexpr uint8_t A    = 0x01;
    constexpr uint8_t B    = 0x02;
    constexpr uint8_t C    = 0x04;
    constexpr uint8_t D    = 0x08;
    constexpr uint8_t E_NEUTRAL = 0x17; // E + ニュートラル
    constexpr uint8_t E_SIDE    = 0x19; // E + 左右
    constexpr uint8_t E_DOWN    = 0x13; // E + 下
    constexpr uint8_t E_DIAG    = 0x10; // E + 斜め
}

namespace MbaaEHoldFlag {
    constexpr uint8_t NONE = 0x00;
    constexpr uint8_t E    = 0x01;
}

// 物理コントローラーの入力状態
struct ControllerState {
    bool isPressedUp    = false;
    bool isPressedDown  = false;
    bool isPressedLeft  = false;
    bool isPressedRight = false;
    bool isPressedA     = false;
    bool isPressedB     = false;
    bool isPressedC     = false;
    bool isPressedD     = false;
    bool isPressedE     = false;
};

// メモリ書き込み用の中間データ
struct MbaaInputData {
    uint8_t directionData = 0x00;
    uint8_t holdData      = 0x00; // 上位4ビット
    uint8_t pushData      = 0x00; // 下位4ビット
    uint8_t e_holdData    = 0x00;
};

// ==========================================================
// 変換関数
// ==========================================================
inline MbaaInputData generateMbaaInput(const ControllerState& cur, const ControllerState& prev) {
    MbaaInputData out;

    bool effLeft  = cur.isPressedLeft  && !cur.isPressedRight;
    bool effRight = cur.isPressedRight && !cur.isPressedLeft;
    bool effUp    = cur.isPressedUp    && !cur.isPressedDown;
    bool effDown  = cur.isPressedDown  && !cur.isPressedUp;

    if (effLeft)  out.directionData |= MbaaDirFlag::LEFT;
    if (effRight) out.directionData |= MbaaDirFlag::RIGHT;
    if (effUp)    out.directionData |= MbaaDirFlag::UP;
    if (effDown)  out.directionData |= MbaaDirFlag::DOWN;

    bool isDiagonal = (effUp || effDown) && (effLeft || effRight);

    // Eボタンの特殊処理
    if (cur.isPressedE) {
        out.e_holdData = MbaaEHoldFlag::E;
        if (!prev.isPressedE) {
            if (isDiagonal) {
                out.pushData = MbaaPushFlag::E_DIAG;
                out.holdData = MbaaHoldFlag::NONE;
            } else if (effDown) {
                out.pushData = MbaaPushFlag::E_DOWN;
                out.holdData = MbaaHoldFlag::AB;
            } else if (effLeft || effRight) {
                out.pushData = MbaaPushFlag::E_SIDE;
                out.holdData = MbaaHoldFlag::AD;
            } else {
                out.pushData = MbaaPushFlag::E_NEUTRAL;
                out.holdData = MbaaHoldFlag::ABC;
            }
        } 
        return out; 
    }

    // 通常ボタン処理
    if (cur.isPressedA) {
        out.holdData |= MbaaHoldFlag::A;
        if (!prev.isPressedA) out.pushData |= MbaaPushFlag::A;
    }
    if (cur.isPressedB) {
        out.holdData |= MbaaHoldFlag::B;
        if (!prev.isPressedB) out.pushData |= MbaaPushFlag::B;
    }
    if (cur.isPressedC) {
        out.holdData |= MbaaHoldFlag::C;
        if (!prev.isPressedC) out.pushData |= MbaaPushFlag::C;
    }
    if (cur.isPressedD) {
        out.holdData |= MbaaHoldFlag::D;
        if (!prev.isPressedD) out.pushData |= MbaaPushFlag::D;
    }

    return out;
}

// ==========================================================
// メモリ注入実行関数
// ==========================================================
inline void injectInputToProcess(const ControllerState& currentState, const ControllerState& previousState) {
    // データ変換
    MbaaInputData input = generateMbaaInput(currentState, previousState);

    // 1. 方向キーの書き込み
    *(reinterpret_cast<uint8_t*>(MBAA_P1_DIR_ADDR)) = input.directionData;

    // 2. ボタン(A,B,C,D)の書き込み
    // 起動(Push)と持続(Hold)が同一アドレス(0055541D)であるため、
    // ビットOR演算( | )で1バイトに合成して書き込む
    // 例: Push A(0x01) と Hold A(0x10) なら 0x11 を書き込む
    uint8_t combinedButtonData = input.pushData | input.holdData;
    *(reinterpret_cast<uint8_t*>(MBAA_P1_BTN_ADDR)) = combinedButtonData;

    // 3. Eキー(特殊)の書き込み
    *(reinterpret_cast<uint8_t*>(MBAA_P1_E_ADDR)) = input.e_holdData;
}
