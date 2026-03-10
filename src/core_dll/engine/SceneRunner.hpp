#pragma once
// ============================================================================
// SceneRunner — ゲームスレッド統合型フレームディスパッチャ
//
// 【設計】
//   Init()  : InitThread から1回だけ呼ばれ、状態変数を初期化する。
//   Step()  : ゲームスレッド (DxHook::Hooked_EndScene) から毎フレーム呼ばれ、
//             1フレーム分のロジック処理を実行する。
//   IsReady(): Init() 完了後に true を返す。EndScene から Step() を呼ぶ前に確認。
//
// 【旧設計との違い】
//   旧: Run() が CreateThread で別スレッドの while ループとして動作
//        → ゲームメモリの読み書きがスレッド競合
//   新: Step() がゲームスレッドの EndScene コールバック内で動作
//        → ゲームと同一スレッドで安全にメモリアクセス
// ============================================================================

#include "core_dll/engine/MatchContext.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace cccaster::sync { class RollbackEngine; }

namespace cccaster::domain::session {

class SceneRunner {
public:
    /// @brief 送信関数の型（レガシー — 現在はNetplaySessionに完全委譲のため未使用）
    using SendFunc = std::function<void(const std::vector<uint8_t>&)>;

    /// @brief 初期化（InitThread から1回だけ呼ぶ）
    static void Init(MatchContext& ctx, SendFunc send = nullptr);

    /// @brief 1フレーム分の処理（ゲームスレッドの EndScene から毎フレーム呼ぶ）
    static void Step();

    /// @brief Init() 完了済みか
    static bool IsReady();
};

} // namespace cccaster::domain::session

