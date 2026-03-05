// ============================================================================
// State_Ui_Logic.cpp — UI状態データ管理の実装
// ============================================================================

#include "core_dll/feature_overlay_ui/State_Ui_Logic.hpp"
#include "core_dll/feature_overlay_ui/overlay/OverlayRenderer.hpp"

namespace cccaster::domain::ui {

// --- static 変数定義 ---
int      StateUiLogic::s_delay = 0;
int      StateUiLogic::s_rollback = 0;
uint64_t StateUiLogic::s_delayPopupEnd = 0;
uint64_t StateUiLogic::s_rollbackPopupEnd = 0;
float    StateUiLogic::s_pingHistory[METRICS_HISTORY_SIZE] = {0};
float    StateUiLogic::s_jitterHistory[METRICS_HISTORY_SIZE] = {0};
int      StateUiLogic::s_historyIndex = 0;
float    StateUiLogic::s_worstPing = 0.0f;
float    StateUiLogic::s_worstJitter = 0.0f;
double   StateUiLogic::s_fps = 60.0;
int64_t  StateUiLogic::s_frameTimeUs = 16666;
float    StateUiLogic::s_timeOffsetMs = 0.0f;
bool     StateUiLogic::s_showMapping = false;

// --- D/R 設定 ---
void StateUiLogic::SetDelay(int value) { s_delay = value; }
void StateUiLogic::SetRollback(int value) { s_rollback = value; }
int  StateUiLogic::GetDelay() { return s_delay; }
int  StateUiLogic::GetRollback() { return s_rollback; }

// --- D/R 変更演出タイマー ---
void StateUiLogic::NotifyDelayChanged() {
    s_delayPopupEnd = cccaster::overlay::OverlayRenderer::GetTimeMs() + POPUP_DURATION_MS;
}
void StateUiLogic::NotifyRollbackChanged() {
    s_rollbackPopupEnd = cccaster::overlay::OverlayRenderer::GetTimeMs() + POPUP_DURATION_MS;
}
bool StateUiLogic::IsDelayPopupActive() {
    return cccaster::overlay::OverlayRenderer::GetTimeMs() < s_delayPopupEnd;
}
bool StateUiLogic::IsRollbackPopupActive() {
    return cccaster::overlay::OverlayRenderer::GetTimeMs() < s_rollbackPopupEnd;
}

// --- ネットワークメトリクス ---
void StateUiLogic::UpdateNetworkMetrics(float pingMs, float jitterMs) {
    s_pingHistory[s_historyIndex] = pingMs;
    s_jitterHistory[s_historyIndex] = jitterMs;
    s_historyIndex = (s_historyIndex + 1) % METRICS_HISTORY_SIZE;

    float maxPing = 0.0f, maxJitter = 0.0f;
    for (int i = 0; i < METRICS_HISTORY_SIZE; ++i) {
        if (s_pingHistory[i] > maxPing) maxPing = s_pingHistory[i];
        if (s_jitterHistory[i] > maxJitter) maxJitter = s_jitterHistory[i];
    }
    s_worstPing = maxPing;
    s_worstJitter = maxJitter;
}
float StateUiLogic::GetWorstPing() { return s_worstPing; }
float StateUiLogic::GetWorstJitter() { return s_worstJitter; }

// --- フレーム情報 ---
void  StateUiLogic::SetFps(double fps) { s_fps = fps; }
void  StateUiLogic::SetFrameTimeUs(int64_t us) { s_frameTimeUs = us; }
void  StateUiLogic::SetTimeOffsetMs(float ms) { s_timeOffsetMs = ms; }
double StateUiLogic::GetFps() { return s_fps; }
int64_t StateUiLogic::GetFrameTimeUs() { return s_frameTimeUs; }
float StateUiLogic::GetTimeOffsetMs() { return s_timeOffsetMs; }

// --- マッピングウィンドウ ---
void StateUiLogic::ToggleMappingWindow() { s_showMapping = !s_showMapping; }
bool StateUiLogic::IsMappingWindowOpen() { return s_showMapping; }
void StateUiLogic::CloseMappingWindow() { s_showMapping = false; }

} // namespace cccaster::domain::ui
