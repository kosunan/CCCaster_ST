#pragma once
// ============================================================================
// FastBootRunner — ゲーム起動高速化（メニュー自動遷移）
//
// 【責務】
//   裏スレッドでゲームメモリを監視し、タイトル画面 → キャラクターセレクト
//   までの遷移を自動化する。ロゴ演出のスキップ、描画OFF による高速化、
//   メニュー入力の偽造を行う。
//
// 【設計】
//   - Start() で裏スレッド起動、キャラセレ到達で自動停止
//   - Stop() で明示停止（DLLアンロード時）
//   - スレッド内で MemoryPatcher を使ってゲームメモリを読み書き
//
// 【依存関係】
//   - MemoryPatcher  : メモリ読み書きプリミティブ
//   - MbaaConstants  : MBAA 固有アドレス
//   - IpcData        : ターゲットゲームモード
// ============================================================================

#include "shared_contracts/IpcData.hpp"
#include <windows.h>

namespace cccaster::game_memory {

class FastBootRunner {
public:
    /// 裏スレッドで FastBoot を開始する。
    /// @param targetMode 目標のゲームモード (Versus/Training 等)
    static void Start(cccaster::public_api::IpcGameMode targetMode);

    /// スレッド停止（DLLアンロード時に呼び出す）。
    static void Stop();

private:
    /// スレッドエントリポイント
    static DWORD WINAPI ThreadFunc(LPVOID lpParam);

    /// FastBoot ループ本体
    static void RunLoop(cccaster::public_api::IpcGameMode targetMode);

    static HANDLE s_hThread;
    static bool   s_running;
    static cccaster::public_api::IpcGameMode s_targetMode;
};

} // namespace cccaster::game_memory
