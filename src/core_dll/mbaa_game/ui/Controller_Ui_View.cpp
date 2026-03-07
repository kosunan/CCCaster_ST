// ============================================================================
// Controller_Ui_View.cpp — マッピング画面 描画専用実装
// ============================================================================
//
// 【責務】
//   ControllerUiLogic から状態を取得して ImGui で描画するだけ。
//   入力処理・状態変更は一切行わない。
// ============================================================================

#include "core_dll/mbaa_game/ui/Controller_Ui_View.hpp"
#include "core_dll/mbaa_game/ui/Controller_Ui_Logic.hpp"
#include "core_dll/fg_netplay/overlay/OverlayRenderer.hpp"
#include "core_dll/platform/hooks/DirectInputHook.hpp"
#include <imgui.h>
#include <cmath>

using L = cccaster::domain::ui::ControllerUiLogic;
using cccaster::overlay::OverlayRenderer;

namespace cccaster::domain::ui {

// ============================================================================
// バインドリスト描画ユーティリティ
// ============================================================================

static void DrawBindList(int pos, const std::string* binds,
                         ImVec4 hiliteColor, bool rightAlign) {
    const char* const* names = L::GetGameInputNames();
    for (int i = 0; i < L::NUM_GAME_INPUTS; ++i) {
        int currentListIndex = i + 1;
        bool isSelected = (currentListIndex == pos);
        std::string val = binds[i].empty() ? "..." : binds[i];
        ImVec4 normalCol  = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        ImVec4 textCol    = isSelected ? hiliteColor : normalCol;

        if (isSelected) ImGui::SetWindowFontScale(1.1f);

        if (!rightAlign) {
            std::string prefix = isSelected ? "> " : "  ";
            ImGui::TextColored(textCol, "%s%-12s : %s", prefix.c_str(), names[i], val.c_str());
        } else {
            std::string suffix  = isSelected ? " <" : "  ";
            std::string rowText = val + " : " + names[i] + suffix;
            float tw = ImGui::CalcTextSize(rowText.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetColumnWidth() - tw);
            ImGui::TextColored(textCol, "%s", rowText.c_str());
        }

        if (isSelected) ImGui::SetWindowFontScale(1.0f);
        else            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
    }

    // 「Finish and Save」行
    bool doneSelected = (pos == L::NUM_GAME_INPUTS + 1);
    ImVec4 doneColor = doneSelected ? hiliteColor : ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    if (doneSelected) ImGui::SetWindowFontScale(1.1f);

    if (!rightAlign) {
        std::string prefix = doneSelected ? "> " : "  ";
        ImGui::TextColored(doneColor, "%sFinish and Save", prefix.c_str());
    } else {
        std::string suffix  = doneSelected ? " <" : "  ";
        std::string rowText = std::string("Finish and Save") + suffix;
        float tw = ImGui::CalcTextSize(rowText.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetColumnWidth() - tw);
        ImGui::TextColored(doneColor, "%s", rowText.c_str());
    }

    if (doneSelected) ImGui::SetWindowFontScale(1.0f);
}

// ============================================================================
// デバイス選択テーブル描画
// ============================================================================

static void DrawDeviceSelectionTable() {
    auto devices = cccaster::game_interface::DirectInputHook::GetConnectedDevices();

    int p1Id = L::GetP1JoyId();
    int p2Id = L::GetP2JoyId();
    int p1Pos = L::GetP1Position();
    int p2Pos = L::GetP2Position();

    static ImGuiTableFlags table_flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

    if (ImGui::BeginTable("DeviceSelectionTable", 3, table_flags)) {
        ImGui::TableSetupColumn("P1",   ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("List", ImGuiTableColumnFlags_WidthStretch, 0.3f);
        ImGui::TableSetupColumn("P2",   ImGuiTableColumnFlags_WidthStretch, 0.35f);

        // ヘッダ行
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "PLAYER 1 CONTROLLER");

        ImGui::TableSetColumnIndex(1);
        const char* titleMid = "AVAILABLE DEVICES";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetColumnWidth() - ImGui::CalcTextSize(titleMid).x) * 0.5f);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", titleMid);

        ImGui::TableSetColumnIndex(2);
        const char* titleP2 = "PLAYER 2 CONTROLLER";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize(titleP2).x - 10.0f);
        ImGui::TextColored(ImVec4(0.3f, 0.6f, 1.0f, 1.0f), "%s", titleP2);

        // 区切り線
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Separator();
        ImGui::TableSetColumnIndex(1); ImGui::Separator();
        ImGui::TableSetColumnIndex(2); ImGui::Separator();

        // コンテンツ行
        ImGui::TableNextRow();

        auto findDevName = [&](int id) -> const char* {
            for (const auto& d : devices) { if (d.id == id) return d.name; }
            return nullptr;
        };

        // P1列
        ImGui::TableSetColumnIndex(0);
        ImGui::Spacing();
        const char* p1Name = findDevName(p1Id);
        if (p1Name) {
            OverlayRenderer::DrawFittedText(p1Name, ImVec4(1,1,1,1), false);
            if (p1Pos > 0) OverlayRenderer::DrawFittedText("(Binding...)", ImVec4(1.0f, 0.8f, 0.2f, 1.0f), false);
        } else if (p1Id == -2) {
            OverlayRenderer::DrawFittedText("ASCII Keyboard", ImVec4(1,1,1,1), false);
            if (p1Pos > 0) OverlayRenderer::DrawFittedText("(Binding...)", ImVec4(1.0f, 0.8f, 0.2f, 1.0f), false);
        } else {
            static uint32_t s_pulseFrame = 0;
            ++s_pulseFrame;
            float alpha = 0.5f + 0.5f * sinf(s_pulseFrame * (6.2831853f / 60.0f));
            OverlayRenderer::DrawFittedText("< PRESS LEFT TO ASSIGN", ImVec4(1.0f, 0.4f, 0.4f, alpha), false);
        }

        // 中央列
        ImGui::TableSetColumnIndex(1);
        ImGui::Spacing();
        if (p1Id != -2 && p2Id != -2) ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  Keyboard");
        for (const auto& dev : devices) {
            if (dev.id != p1Id && dev.id != p2Id)
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  %s", dev.name);
        }

        // P2列
        ImGui::TableSetColumnIndex(2);
        ImGui::Spacing();
        const char* p2Name = findDevName(p2Id);
        if (p2Name) {
            OverlayRenderer::DrawFittedText(p2Name, ImVec4(1,1,1,1), true);
            if (p2Pos > 0) OverlayRenderer::DrawFittedText("(Binding...)", ImVec4(0.2f, 0.9f, 1.0f, 1.0f), true);
        } else if (p2Id == -2) {
            OverlayRenderer::DrawFittedText("ASCII Keyboard", ImVec4(1,1,1,1), true);
            if (p2Pos > 0) OverlayRenderer::DrawFittedText("(Binding...)", ImVec4(0.2f, 0.9f, 1.0f, 1.0f), true);
        } else {
            static uint32_t s_pulseFrame2 = 0;
            ++s_pulseFrame2;
            float alpha = 0.5f + 0.5f * sinf(s_pulseFrame2 * (6.2831853f / 60.0f));
            OverlayRenderer::DrawFittedText("PRESS RIGHT TO ASSIGN >", ImVec4(0.4f, 0.6f, 1.0f, alpha), true);
        }
        ImGui::EndTable();
    }
}

// ============================================================================
// P1/P2 バインドウィンドウ描画
// ============================================================================

static void DrawP1BindingWindow(float screenWidth) {
    if (L::GetP1Position() == 0) return;

    ImGui::SetNextWindowSize(ImVec2(L::MAPPING_WINDOW_WIDTH * 0.46f, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((screenWidth - L::MAPPING_WINDOW_WIDTH) * 0.5f, -2.0f + 140.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGuiWindowFlags bindFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    OverlayRenderer::PushModernStyle();

    if (ImGui::Begin("P1 Binding", nullptr, bindFlags)) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "PLAYER 1 MAPPING");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTable("P1MappingTable", 1, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("P1", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawBindList(L::GetP1Position(), L::GetP1Binds(), ImVec4(1.0f, 0.8f, 0.2f, 1.0f), false);
            ImGui::EndTable();
        }
    }
    ImGui::End();
    OverlayRenderer::PopModernStyle();
}

static void DrawP2BindingWindow(float screenWidth) {
    if (L::GetP2Position() == 0) return;

    ImGui::SetNextWindowSize(ImVec2(L::MAPPING_WINDOW_WIDTH * 0.46f, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((screenWidth - L::MAPPING_WINDOW_WIDTH) * 0.5f + (L::MAPPING_WINDOW_WIDTH * 0.54f), -2.0f + 140.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGuiWindowFlags bindFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    OverlayRenderer::PushModernStyle();

    if (ImGui::Begin("P2 Binding", nullptr, bindFlags)) {
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::CalcTextSize("PLAYER 2 MAPPING").x - 20.0f);
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "PLAYER 2 MAPPING");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTable("P2MappingTable", 1, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("P2", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawBindList(L::GetP2Position(), L::GetP2Binds(), ImVec4(0.2f, 0.9f, 1.0f, 1.0f), true);
            ImGui::EndTable();
        }
    }
    ImGui::End();
    OverlayRenderer::PopModernStyle();
}

// ============================================================================
// メイン描画関数
// ============================================================================

void ControllerUiView::Draw() {
    // ロジック更新（入力処理・状態変更）
    L::Update();

    float screenWidth = ImGui::GetIO().DisplaySize.x;

    // メインウィンドウ
    ImGui::SetNextWindowSize(ImVec2(L::MAPPING_WINDOW_WIDTH, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((screenWidth - L::MAPPING_WINDOW_WIDTH) * 0.5f, -2.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoNav;

    OverlayRenderer::PushModernStyle();

    if (ImGui::Begin("Controller Mapping", nullptr, flags)) {
        // タイトル
        ImGui::SetWindowFontScale(1.3f);
        float titleWidth = ImGui::CalcTextSize("CONTROLLER CONFIGURATION").x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - titleWidth) * 0.5f);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.9f, 1.0f), "CONTROLLER CONFIGURATION");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Spacing();

        // 操作ガイド
        const char* helperText = "[F4] Close Menu  |  [Enter/Button] Start Mapping";
        float helperWidth = ImGui::CalcTextSize(helperText).x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - helperWidth) * 0.5f);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", helperText);

        ImGui::Separator();
        ImGui::Spacing();

        // デバイス選択テーブル描画
        DrawDeviceSelectionTable();
    }
    ImGui::End();
    OverlayRenderer::PopModernStyle();

    // バインドウィンドウ描画
    DrawP1BindingWindow(screenWidth);
    DrawP2BindingWindow(screenWidth);
}

void ControllerUiView::OnClose() {
    L::ResetBindingState();
    L::SaveDeviceAllocations();
}

} // namespace cccaster::domain::ui
