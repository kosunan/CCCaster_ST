#pragma once
// ============================================================================
// State_Ui_View — 常時表示ステータスバー描画
// ============================================================================
//
// 【責務】
//   キャラセレ・対戦画面で共通表示されるステータスバーの描画。
//   State_Ui_Logic からデータを取得して ImGui で描画する。
// ============================================================================

namespace cccaster::domain::ui {

class StateUiView {
public:
    /// @brief キャラセレ用の常時ステータスバー描画
    /// DLY: 3 [Ctrl]+0-9 | RB: 7 [Alt]+0-9 | PING: 12 | JTR: 3 | [F4]
    static void DrawCharaSelectBar();

    /// @brief 対戦中用の拡張ステータスバー描画
    /// D:3 R:7 | 16666μs | 60.000fps | Δ: +0ms | RTT: 12ms | JTR: 3ms
    static void DrawInGameBar();

    /// @brief D値変更時の大フォントポップアップ
    static void DrawDelayPopup();

    /// @brief R値変更時の大フォントポップアップ
    static void DrawRollbackPopup();
};

} // namespace cccaster::domain::ui
