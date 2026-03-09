#pragma once
// ============================================================================
// InputHelper — ゲームメモリへの入力読み書きユーティリティ
// SceneRunner で定義、各Scene から extern 参照する代わりにこのヘッダで宣言
// ============================================================================

#include <cstdint>

namespace cccaster::domain::session {

// ゲームメモリに P1/P2 入力を書き込む
// format: (direction << 16) | buttons
void WriteInputToMemory(uint32_t p1Input, uint32_t p2Input);

// ローカルプレイヤーの入力をゲームメモリから読み取る
// format: (direction << 16) | buttons
uint32_t ReadLocalInput(bool isHost);

} // namespace cccaster::domain::session
