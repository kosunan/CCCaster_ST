#pragma once
// ============================================================================
// Controller_Ui_Logic — コントローラーマッピング ロジック層
// ============================================================================
//
// 【責務】
//   F4マッピング画面の全状態管理・入力処理・保存ロジック。
//   描画コードは含まない。View が Getter 経由でデータを取得する。
//
// 【元ファイル】
//   ControllerMapper.cpp のファイルスコープ static 変数と
//   Process*/Save*/Reset* メソッドを移植。
// ============================================================================

#include <string>
#include <cstdint>

namespace cccaster::domain::ui {

class ControllerUiLogic {
public:
    static constexpr int NUM_GAME_INPUTS = 13;
    static constexpr float MAPPING_WINDOW_WIDTH = 620.0f;
    static constexpr double MAPPING_START_DEAD_TIME = 0.2;

    // --- 毎フレーム呼ばれるロジック更新 ---
    static void Update();

    // --- 状態リセット ---
    static void ResetBindingState();

    // --- 保存 ---
    static void SaveDeviceAllocations();

    // --- Getter (View が参照) ---
    static int GetP1JoyId();
    static int GetP2JoyId();
    static int GetP1Position();
    static int GetP2Position();
    static const std::string* GetP1Binds();
    static const std::string* GetP2Binds();
    static const std::string& GetP1CachedEdge();
    static const std::string& GetP2CachedEdge();

    /// ゲーム入力名ラベル配列
    static const char* const* GetGameInputNames();

private:
    // --- 内部処理 ---
    static void ProcessDeviceSelectionInput();
    static void ProcessBindingInput(int joyId, int playerIndex, int& pos, std::string* binds);
    static void SaveBinds(int joyId, const std::string& prefix, std::string* binds);

    // --- 状態変数 ---
    static int s_p1JoyId;
    static int s_p2JoyId;
    static int s_p1Position;
    static int s_p2Position;
    static std::string s_p1Binds[NUM_GAME_INPUTS];
    static std::string s_p2Binds[NUM_GAME_INPUTS];
    static double s_p1BindStartTime;
    static double s_p2BindStartTime;
    static std::string s_p1CachedEdge;
    static std::string s_p2CachedEdge;
};

} // namespace cccaster::domain::ui
