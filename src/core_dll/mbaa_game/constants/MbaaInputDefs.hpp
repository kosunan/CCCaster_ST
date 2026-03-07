#pragma once
/**
 * @file MbaaInputDefs.hpp
 * @brief MBAACC 入力アドレス・ボタンビットマスク・入力合成マクロ
 *
 * 【責務】
 *   ゲーム入力に関する定数定義を集約する。
 *   方向キー、ボタン、入力書き込みアドレス、合成マクロを含む。
 *
 * 【分割元】
 *   旧 MbaaConstants.hpp の入力系セクション（L139-L177）を抽出。
 */

#include <cstdint>


// ============================================================================
// 入力アドレス
// ============================================================================

#define CC_PTR_TO_WRITE_INPUT_ADDR  ( ( char * )     0x76E6AC ) // Pointer to the location to write game input
#define CC_P1_OFFSET_DIRECTION      ( 0x18 )                    // Offset to write P1 direction input
#define CC_P1_OFFSET_BUTTONS        ( 0x24 )                    // Offset to write P1 buttons input
#define CC_P2_OFFSET_DIRECTION      ( 0x2C )                    // Offset to write P2 direction input
#define CC_P2_OFFSET_BUTTONS        ( 0x38 )                    // Offset to write P2 buttons input


// ============================================================================
// 方向キービットマスク（テンキー表記、neutral = 0）
// ============================================================================

#define BIT_UP                      ( 0x01 )
#define BIT_DOWN                    ( 0x02 )
#define BIT_LEFT                    ( 0x04 )
#define BIT_RIGHT                   ( 0x08 )


// ============================================================================
// ボタン定義
// ============================================================================

#define CC_BUTTON_A                 ( 0x0010 )
#define CC_BUTTON_B                 ( 0x0020 )
#define CC_BUTTON_C                 ( 0x0008 )
#define CC_BUTTON_D                 ( 0x0004 )
#define CC_BUTTON_E                 ( 0x0080 )
#define CC_BUTTON_AB                ( 0x0040 )
#define CC_BUTTON_START             ( 0x0001 )
#define CC_BUTTON_FN1               ( 0x0100 ) // Control dummy
#define CC_BUTTON_FN2               ( 0x0200 ) // Training reset
#define CC_BUTTON_CONFIRM           ( 0x0400 )
#define CC_BUTTON_CANCEL            ( 0x0800 )
#define CC_PLAYER_FACING            ( 0x0002 )


// ============================================================================
// 入力合成マクロ
// ============================================================================

// 入力合成マクロ: 方向(下位) + ボタン(上位) → 32bit
#define COMBINE_INPUT(direction, buttons) \
    ( static_cast<uint32_t>(direction) | ( static_cast<uint32_t>(buttons) << 8 ) )

// ボタン連打マクロ（奇数フレームのみ入力）
#define RETURN_MASH_INPUT(direction, buttons) \
    do { static int _mashFrame = 0; return (_mashFrame++ % 2) \
         ? COMBINE_INPUT(direction, buttons) : 0; } while(0)
