#pragma once
// ============================================================================
// Platform — OS 依存処理の唯一の窓口
//
// 【なぜ存在するか】
//   実機用 DLL は Windows 専用だが、harness（ゲーム無しで同期ロジックを通しで
//   走らせる実行ファイル）は Linux でも動かしたい。両方でビルドしたいコードが
//   `windows.h` を直接叩いていると、その1行のためにファイル全体が Windows 専用に
//   なる。時刻取得・待機・CPU 緩和のような「OS ごとに書き方が違うだけで意味は同じ」
//   処理をここに集約し、呼び出し側から `#ifdef _WIN32` を消す。
//
// 【設計上の制約】
//   このヘッダは `windows.h` を include しない。Win32 の型が漏れると、
//   include した側が結局 Windows 専用になるため。実装は Platform.cpp に隠す。
//
// 【SleepMs と RealSleepMs の違い — 重要】
//   実機の DLL では `TimeHooks` が MinHook で kernel32!Sleep 本体をインライン
//   フックしており、`dllmain.cpp` が起動時に `SetSleepBypass(true)` を恒久設定
//   している。その結果、**自分の DLL 内で呼んだ `Sleep(1)` すら `Sleep(0)` に
//   化ける**（＝完全なビジースピン）。
//
//     SleepMs()     … フック後の Sleep。実機では 0ms 即リターンになる。
//                     既存の呼び出し箇所はこちらに対応する（現状維持）。
//     RealSleepMs() … フック前の本物の Sleep。CPU を返したい箇所はこちら。
//
//   harness には TimeHooks が入っていない（stub）ので、両者は同じ挙動になる。
//   Linux では両者とも本物の待機。
// ============================================================================

#include <cstdint>

namespace cccaster::platform {

// ── 時刻 ───────────────────────────────────────────────────
/// 実時間の単調時刻 [μs]。
/// Windows では TimeHooks にフックされる前の QPC を使うため、
/// ゲーム側の 1000 倍速タイマーの影響を受けない。
int64_t RealMonotonicUs();

// ── 待機 ───────────────────────────────────────────────────
/// フック後の Sleep（実機ではバイパスされて 0ms になりうる）。
void SleepMs(uint32_t ms);

/// フック前の本物の Sleep。CPU を明示的に手放したい箇所で使う。
void RealSleepMs(uint32_t ms);

/// スピンウェイト中に CPU に譲る（x86 では PAUSE 命令）。
void CpuRelax();

// ── タイマー分解能 ─────────────────────────────────────────
/// Windows のタイマー分解能を 1ms に上げる。Linux では何もしない。
/// 取得したら必ず EndHighResolutionTimers() で戻すこと。
void BeginHighResolutionTimers();
void EndHighResolutionTimers();

// ── 中断要求 ───────────────────────────────────────────────
/// ユーザーによる中断が要求されているか。
/// Windows: F12 の押下。Linux: SIGINT / SIGTERM を受けたか。
bool IsAbortRequested();

/// Linux でシグナルハンドラを登録する。Windows では何もしない。
/// harness の main 冒頭で呼ぶ。
void InstallAbortHandler();

// ── プロセス終了 ───────────────────────────────────────────
/// 自プロセスを即座に終了する（デストラクタも atexit も走らない）。
/// 実機では注入先の MBAA.exe ごと落ちる。
[[noreturn]] void TerminateSelf();

} // namespace cccaster::platform
