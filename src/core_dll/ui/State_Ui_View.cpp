// ============================================================================
// State_Ui_View.cpp — 常時表示ステータスバー描画の実装
// ============================================================================

#include "core_dll/ui/State_Ui_View.hpp"
#include "core_dll/ui/State_Ui_Logic.hpp"
#include "core_dll/ui/OverlayRenderer.hpp"
#include <imgui.h>
#include <cstdio>

namespace cccaster::domain::ui {

// ============================================================================
// キャラセレ用ステータスバー
// ============================================================================
void StateUiView::DrawCharaSelectBar() {
    float width = ImGui::GetIO().DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, -2.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove;

    cccaster::overlay::OverlayRenderer::PushModernStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.0f, 4.0f));

    if (ImGui::Begin("##StateBar_CharaSel", nullptr, flags)) {
        ImVec4 baseColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

        ImGui::TextColored(baseColor, "DLY:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%d", StateUiLogic::GetDelay());
        ImGui::SameLine();
        ImGui::TextColored(baseColor, "[Ctrl]+0-9| RB:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%d", StateUiLogic::GetRollback());
        ImGui::SameLine();
        ImGui::TextColored(baseColor, "[Alt]+0-9| PING: %.0f  |  JTR: %.0f  |  [F4] Controller Setup",
                           StateUiLogic::GetWorstPing(), StateUiLogic::GetWorstJitter());
    }
    ImGui::End();

    ImGui::PopStyleVar();
    cccaster::overlay::OverlayRenderer::PopModernStyle();
}

// ============================================================================
// 対戦用拡張ステータスバー
// ============================================================================
void StateUiView::DrawInGameBar() {
    float width = ImGui::GetIO().DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, -2.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove;

    cccaster::overlay::OverlayRenderer::PushModernStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));

    if (ImGui::Begin("##StateBar_InGame", nullptr, flags)) {
        ImVec4 baseColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        ImVec4 valueColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);

        // D:3 R:7
        ImGui::TextColored(baseColor, "D:");
        ImGui::SameLine(0, 0);
        ImGui::TextColored(valueColor, "%d", StateUiLogic::GetDelay());
        ImGui::SameLine();
        ImGui::TextColored(baseColor, "R:");
        ImGui::SameLine(0, 0);
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%d", StateUiLogic::GetRollback());
        ImGui::SameLine();

        // 16666μs
        ImGui::TextColored(baseColor, "|");
        ImGui::SameLine();
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)StateUiLogic::GetFrameTimeUs());
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", buf);
        ImGui::SameLine(0, 0);
        ImGui::TextColored(baseColor, "us");
        ImGui::SameLine();

        // 60.000fps
        ImGui::TextColored(baseColor, "|");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%.3f", StateUiLogic::GetFps());
        ImGui::SameLine(0, 0);
        ImGui::TextColored(baseColor, "fps");
        ImGui::SameLine();

        // Δ: +0ms
        ImGui::TextColored(baseColor, "|");
        ImGui::SameLine();
        float offset = StateUiLogic::GetTimeOffsetMs();
        ImVec4 offsetColor = (offset > 5.0f || offset < -5.0f)
                             ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                             : ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
        char offsetBuf[16];
        snprintf(offsetBuf, sizeof(offsetBuf), "%+.0f", offset);
        ImGui::TextColored(baseColor, "D:");
        ImGui::SameLine(0, 0);
        ImGui::TextColored(offsetColor, "%s", offsetBuf);
        ImGui::SameLine(0, 0);
        ImGui::TextColored(baseColor, "ms");
        ImGui::SameLine();

        // RTT: 12ms
        ImGui::TextColored(baseColor, "|");
        ImGui::SameLine();
        ImGui::TextColored(baseColor, "RTT:");
        ImGui::SameLine(0, 0);
        ImGui::TextColored(valueColor, "%.0f", StateUiLogic::GetWorstPing());
        ImGui::SameLine(0, 0);
        ImGui::TextColored(baseColor, "ms");
        ImGui::SameLine();

        // JTR: 3ms
        ImGui::TextColored(baseColor, "|");
        ImGui::SameLine();
        ImGui::TextColored(baseColor, "JTR:");
        ImGui::SameLine(0, 0);
        ImGui::TextColored(valueColor, "%.0f", StateUiLogic::GetWorstJitter());
        ImGui::SameLine(0, 0);
        ImGui::TextColored(baseColor, "ms");
    }
    ImGui::End();

    ImGui::PopStyleVar();
    cccaster::overlay::OverlayRenderer::PopModernStyle();
}

// ============================================================================
// D値変更ポップアップ
// ============================================================================
void StateUiView::DrawDelayPopup() {
    float width = ImGui::GetIO().DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, 50.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoMove;

    cccaster::overlay::OverlayRenderer::PushModernStyle();
    if (ImGui::Begin("##DelayPopup", nullptr, flags)) {
        if (ImGui::GetIO().Fonts->Fonts.Size > 1)
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
        else
            ImGui::SetWindowFontScale(1.5f);

        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Input Delay: %d", StateUiLogic::GetDelay());

        if (ImGui::GetIO().Fonts->Fonts.Size > 1)
            ImGui::PopFont();
        else
            ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    cccaster::overlay::OverlayRenderer::PopModernStyle();
}

// ============================================================================
// R値変更ポップアップ
// ============================================================================
void StateUiView::DrawRollbackPopup() {
    float width = ImGui::GetIO().DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, 50.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoMove;

    cccaster::overlay::OverlayRenderer::PushModernStyle();
    if (ImGui::Begin("##RollbackPopup", nullptr, flags)) {
        if (ImGui::GetIO().Fonts->Fonts.Size > 1)
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
        else
            ImGui::SetWindowFontScale(1.5f);

        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Max Rollback: %d", StateUiLogic::GetRollback());

        if (ImGui::GetIO().Fonts->Fonts.Size > 1)
            ImGui::PopFont();
        else
            ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    cccaster::overlay::OverlayRenderer::PopModernStyle();
}

} // namespace cccaster::domain::ui
