#pragma once

#include <cstdint>
#include "core_dll/game_memory_accessor/MbaaConstants.hpp"

namespace cccaster::sync {

    // ====================================================================
    // InputFilter — Scene別入力フィルタ + オーバーレイ入力消費パターン
    //
    // [2段階処理]
    //   1. オーバーレイが rawInput を受け取り、使うボタンを消費(consume)
    //   2. 残りをゲームメモリに書き込み（Scene別フィルタ適用後）
    //
    // [Scene別ルール]
    //   CharaSelect: オーバーレイが全ボタン使用可能（バインド設定等）
    //                消費後の残りをゲームに渡す
    //   Loading:     フィルタなし（ディレイ付き入力交換で処理）
    //   InGame:      FilterBlockedButtons（START/FN1/FN2除去）
    //                オーバーレイは表示のみ（値変更不可）
    //   Rematch:     フィルタなし（自由操作）
    // ====================================================================
    class InputFilter {
    public:
        // ★ 対戦中にブロックするボタン（Start/FN1/FN2）
        static constexpr uint16_t BLOCKED_BUTTONS = CC_BUTTON_START | CC_BUTTON_FN1 | CC_BUTTON_FN2;

        // --- 対戦中用: 危険ボタンフィルタ ---
        static inline uint16_t FilterBlockedButtons(uint16_t input) {
            return input & ~BLOCKED_BUTTONS;
        }

        // --- オーバーレイ入力消費 ---
        // オーバーレイが使ったボタンをマスクで返す
        // consumedMask: オーバーレイが消費したボタンのビットマスク
        // 戻り値: 消費されなかった残りの入力
        static inline uint16_t ConsumeOverlayInput(uint16_t rawInput, uint16_t consumedMask) {
            return rawInput & ~consumedMask;
        }

        // --- Scene別フィルタ適用 ---
        // overlayActive: オーバーレイが開いているか
        // overlayConsumed: オーバーレイが消費したボタンマスク
        //
        // CharaSelect: オーバーレイ消費後の残りを通す
        // InGame:      FilterBlockedButtons適用（オーバーレイは表示のみ）
        // Loading/Rematch: そのまま通す
        enum class SceneType : uint8_t {
            CharaSelect,
            Loading,
            InGame,
            Rematch
        };

        static inline uint16_t FilterForScene(
            SceneType scene,
            uint16_t rawInput,
            bool overlayActive,
            uint16_t overlayConsumed)
        {
            switch (scene) {
            case SceneType::CharaSelect:
                // オーバーレイが消費した分を除いた残りをゲームに渡す
                if (overlayActive) {
                    return ConsumeOverlayInput(rawInput, overlayConsumed);
                }
                return rawInput;  // オーバーレイ非表示時は全入力通す

            case SceneType::InGame:
                // 対戦中は危険ボタンブロック（オーバーレイは表示のみ）
                return FilterBlockedButtons(rawInput);

            case SceneType::Loading:
            case SceneType::Rematch:
            default:
                return rawInput;  // そのまま通す
            }
        }

        // キャラクターセレクト画面での入力をフィルタリングする（レガシー互換）
        static uint16_t FilterCharaSelectInput(int playerIndex, uint16_t rawInput, uint32_t framesInCharaSelect);
    };

} // namespace cccaster::sync
