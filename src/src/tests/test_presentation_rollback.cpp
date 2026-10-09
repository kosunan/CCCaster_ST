#include "core_dll/rollback/PresentationRollback.hpp"
#include <array>
#include <cstring>
#include <cstdio>
using cccaster::sync::PresentationRollback;
struct Memory : cccaster::game_interface::IGameMemory {
    // 時刻・予約動作・乱数・リプレイ末尾・生入力・標準入力履歴を模擬。
    std::array<uint32_t, 6> state{1, 2, 3, 4, 5, 6};
    bool muted = false, saveOk = true, loadOk = true, corrupt = false;
    bool IsAvailable() const override { return true; }
    uint32_t GameMode() const override { return 1; }
    uint8_t IntroState() const override { return 0; }
    uint32_t WorldTimer() const override { return state[0]; }
    uint32_t RealTimer() const override { return state[0]; }
    uint32_t MenuStateCounter() const override { return 0; }
    void WriteInput(cccaster::game_interface::GameInput, cccaster::game_interface::GameInput) override {
        ++state[4]; // 保存・復元が新たな入力を注入したら元の状態との比較に失敗する。
    }
    size_t PresentationSnapshotSize() const override { return sizeof(state); }
    bool SavePresentationSnapshot(std::span<char> out) override {
        std::memcpy(out.data(), state.data(), sizeof(state)); return saveOk;
    }
    bool LoadPresentationSnapshot(std::span<char> in) override {
        if (!loadOk) return false;
        std::memcpy(state.data(), in.data(), sizeof(state));
        if (corrupt) ++state[5];
        return true;
    }
    void SetPresentationPreview(bool active) override { muted = active; }
};
int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    Memory mem;
    PresentationRollback preview;
    const auto initial = mem.state;
    const auto originalRounding = std::fegetround();
    std::fesetround(FE_DOWNWARD);
    check(!preview.Restore(mem), "未開始の復元を拒否する");
    for (int frame = 0; frame < 30; ++frame) {
        check(preview.Begin(mem) && preview.Pending() && mem.muted, "保存後に表示専用音声抑止を有効にする");
        check(!preview.Begin(mem), "二重開始で保存した確定状態を上書きしない");
        mem.state = {10, 20, 30, 40, 50, 60};
        std::fesetround(FE_UPWARD);
        check(preview.Restore(mem) && !preview.Pending() && !mem.muted, "通常更新へ戻る前に復元・抑止解除する");
        check(mem.state == initial, "予約・乱数・記録末尾・入力履歴を全て戻す");
        check(std::fegetround() == FE_DOWNWARD, "浮動小数点の丸め状態を戻す");
    }
    mem.saveOk = false;
    check(!preview.Begin(mem) && !preview.Pending() && !mem.muted, "保存失敗なら余分な更新を始めない");
    mem.saveOk = true;
    check(preview.Begin(mem), "保存失敗後の正常開始");
    mem.loadOk = false;
    check(!preview.Restore(mem) && preview.Pending() && mem.muted, "復元失敗時は確定更新へ進めない");
    mem.loadOk = true; mem.corrupt = true;
    check(!preview.Restore(mem) && preview.Pending() && mem.muted, "成功を返す不完全な復元も検出する");
    mem.corrupt = false;
    check(preview.Restore(mem) && mem.state == initial, "保存状態は失敗後も保持する");
    std::fesetround(originalRounding);
    return failures ? 1 : 0;
}
