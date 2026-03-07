// ============================================================================
// Rematch_Ui_View.cpp — 再戦画面 描画実装
// ============================================================================
//
// TODO: エモーション選択の完全な実装は後続タスクで行う。
//       現段階ではプレースホルダUIを表示。
// ============================================================================

#include "core_dll/fg_netplay/overlay/Rematch_Ui_View.hpp"
#include "core_dll/fg_netplay/overlay/OverlayRenderer.hpp"
#include <imgui.h>

namespace cccaster::domain::ui {

void RematchUiView::Draw() {
    float width = ImGui::GetIO().DisplaySize.x;
    float height = ImGui::GetIO().DisplaySize.y;
    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove;

    cccaster::overlay::OverlayRenderer::PushModernStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 15.0f));

    if (ImGui::Begin("##RematchUI", nullptr, flags)) {
        ImVec4 titleColor = ImVec4(1.0f, 0.9f, 0.3f, 1.0f);
        ImVec4 baseColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

        ImGui::TextColored(titleColor, "REMATCH?");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(baseColor, "[1] Rematch");
        ImGui::TextColored(baseColor, "[2] Quit");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[3] GG!   [4] Nice!   [5] Fun!");
    }
    ImGui::End();

    ImGui::PopStyleVar();
    cccaster::overlay::OverlayRenderer::PopModernStyle();
}

} // namespace cccaster::domain::ui
