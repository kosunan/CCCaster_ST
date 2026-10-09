#pragma once
#include "core_dll/mbaa_mem/NativeDisplayOptions.hpp"
#include <cstdint>

namespace cccaster::game_interface::native_resolution {
ScreenResolution Read();
bool Request(int direction);
// Steamの毎描画の自動比率上書きを止め、標準の7種類の描画分岐を使用する。
// 設定を手動変更するまではSteam既定の自動選択を保つ。
bool EnableAspectSelection();
bool Restore(int width, int height);
// DxHookから実Resetの成否と実バックバッファ寸法を受け取る。
void ResetFinished(long result, unsigned width, unsigned height);
}
