// ============================================================================
// InputFilter.cpp — Scene別入力フィルタ実装
//
// 【FilterCharaSelectInput について】
//   Filter A（150F確認ボタン封印）・Filter B（メインメニュー移行防止）は
//   本関数に実装されている（レガシー互換 API）。
//
//   Filter C（3F バッファガード）は SceneCharaSelect.cpp 内で
//   Scene ステートと合わせて管理されており、本 API には含まれない。
//   本 API はダイレクトに playerIndex/rawInput/framesInCharaSelect を
//   受け取る軽量ヘルパーとして維持する。
// ============================================================================

#include "core_dll/pure_sync_engine/InputFilter.hpp"
#include "core_dll/game_memory_accessor/MbaaConstants.hpp"

namespace cccaster::sync {

    uint16_t InputFilter::FilterCharaSelectInput(int playerIndex, uint16_t rawInput, uint32_t framesInCharaSelect) {
        uint16_t filteredInput = rawInput;

        // Filter A: キャラセレ開始後150FはAボタン・決定を無効化
        //   理由: ムーンセレクターの同期ズレを回避するためのワークアラウンド
        if (framesInCharaSelect < 150) {
            filteredInput &= ~(CC_BUTTON_A | CC_BUTTON_CONFIRM);
        }

        // Filter B: キャラクターを選択中（ムーンやカラーではなく）の場合はキャンセル（戻る）を無効化
        //   理由: キャラセレからメインメニューに戻る操作によるセッション断絶を防止
        uint32_t* selectorModeAddr = (playerIndex == 0) ?
            CC_P1_SELECTOR_MODE_ADDR :
            CC_P2_SELECTOR_MODE_ADDR;

        uint32_t selectorMode = *selectorModeAddr;

        if (selectorMode == CC_SELECT_CHARA) {
            filteredInput &= ~(CC_BUTTON_B | CC_BUTTON_CANCEL);
        }

        // NOTE: Filter C（3F バッファガード）は SceneCharaSelect.cpp 内で
        //   Scene ステート（s_frameCount, s_lastDirChangedFrame 等）と
        //   合わせて実装されており、この関数では行わない。
        //   SceneCharaSelect::FilterCharaSelectInput() 内のフィルタC実装を参照すること。

        return filteredInput;
    }

} // namespace cccaster::sync
