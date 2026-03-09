#pragma once
// ====================================================================
// ControllerInputSim.hpp — コントローラ入力シミュレーター (フェーズD)
//
// 画面別のリアル互換入力を生成する。
// DummySceneCharaSelect / DummySceneInGame / DummySceneRematch から呼ばれる。
// ====================================================================

#include <cstdint>

namespace dummy_peer {

class ControllerInputSim {
public:
    // === MBAA ボタン定数 (CC_BUTTON_* 互換) ===
    static constexpr uint16_t BTN_A       = 0x0001;
    static constexpr uint16_t BTN_B       = 0x0002;
    static constexpr uint16_t BTN_C       = 0x0004;
    static constexpr uint16_t BTN_D       = 0x0008;
    static constexpr uint16_t BTN_E       = 0x0010;
    static constexpr uint16_t BTN_AB      = 0x0020;
    static constexpr uint16_t BTN_START   = 0x0040;
    static constexpr uint16_t BTN_CONFIRM = 0x0200;
    static constexpr uint16_t BTN_CANCEL  = 0x0400;

    // === 方向定数 (numpad 形式) ===
    static constexpr uint16_t DIR_NEUTRAL = 5;
    static constexpr uint16_t DIR_UP      = 8;
    static constexpr uint16_t DIR_DOWN    = 2;
    static constexpr uint16_t DIR_LEFT    = 4;
    static constexpr uint16_t DIR_RIGHT   = 6;

    // ================================================================
    // GenerateCharaSelectInput — キャラセレ入力シーケンス
    //
    // フレーム番号に応じたリアルな選択シーケンスを生成する:
    //   Phase 1 ( 0〜149F):  Filter A互換 — ニュートラル (確定ボタン無効期間)
    //   Phase 2 (150〜179F): カーソル移動 (方向入力)
    //   Phase 3 (180〜210F): 方向キー放し → Filter C の33Fガード待ち
    //   Phase 4 (211F〜):    A/Confirm 連打 → キャラ確定
    //
    // @param frameInPhase  キャラセレ開始からのフレーム数
    // @return uint16_t     ボタン入力値 (上位bit=方向, 下位bit=ボタン)
    // ================================================================
    uint16_t GenerateCharaSelectInput(uint32_t frameInPhase);

    // ================================================================
    // GenerateLoadingInput — ロード画面入力
    //
    // ロード中は操作不要なのでニュートラルを返す。
    // @return 0 (ニュートラル)
    // ================================================================
    uint16_t GenerateLoadingInput() { return 0; }

    // ================================================================
    // GenerateGameInput — InGame ランダム戦闘入力
    //
    // フレームごとに方向+ボタンをランダムに組み合わせて返す。
    // 本番 SceneRunner の GenerateRandomTestInput() と同等ロジック。
    //
    // @param seed  ランダムシード値 (フレーム番号等を渡す)
    // @return uint32_t  P1 入力 (上位16bit=方向, 下位16bit=ボタン)
    // ================================================================
    uint32_t GenerateGameInput(uint32_t seed);

    // ================================================================
    // GenerateRematchSelection — リマッチ選択
    //
    // Config の rematchChoice に基づくメニューインデックスを返す。
    //   0: 再戦 (LOADING に遷移)
    //   1: キャラセレに戻る (CS_SELECTING に遷移)
    //
    // @param rematchChoice  Config.rematchChoice の値
    // @return int8_t        メニューインデックス
    // ================================================================
    int8_t GenerateRematchSelection(int rematchChoice) {
        return static_cast<int8_t>(rematchChoice & 1);
    }

private:
    // 簡易 LCG 乱数
    uint32_t _rng = 12345;
    uint32_t NextRng() {
        _rng = _rng * 1664525u + 1013904223u;
        return _rng;
    }
};

} // namespace dummy_peer
