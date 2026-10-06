#include "core_dll/hook/CompiledInputBindings.hpp"
#include "test_support.hpp"
#include <map>

using namespace cccaster::input;

int main() {
    const auto key = [](const std::string &name) { return name == "F1" ? 112 : name == "S" ? 83 : 0; };
    CC_CASE("全ボタン・軸・POVと既存の数値接頭辞を保持する");
    for (int i = 0; i < 128; ++i) {
        auto b = CompileBinding("B" + std::to_string(i), false, key);
        CC_CHECK(b.kind == BindingKind::Button); CC_CHECK_EQ(b.index, i);
    }
    for (int i = 0; i < 8; ++i)
        for (const int sign : {-1, 1}) {
            auto b = CompileBinding("A" + std::to_string(i) + (sign > 0 ? "+" : "-"), false, key);
            CC_CHECK(b.kind == BindingKind::Axis); CC_CHECK_EQ(b.index, i); CC_CHECK_EQ(b.direction, sign);
        }
    for (int i = 0; i < 4; ++i)
        for (const int dir : {8, 2, 4, 6}) {
            auto b = CompileBinding("H" + std::to_string(i) + "_" + std::to_string(dir), false, key);
            CC_CHECK(b.kind == BindingKind::Hat); CC_CHECK_EQ(b.index, i); CC_CHECK_EQ(b.direction, dir);
        }
    CC_CHECK_EQ(CompileBinding("B 12suffix", false, key).index, 12);
    CC_CHECK_EQ(CompileBinding("A2?", false, key).direction, -1);
    CC_CHECK_EQ(CompileBinding("H0_8suffix", false, key).direction, 8);
    CC_CASE("空・壊れた設定・範囲外は無入力");
    for (const auto *text : {"", "B", "B-1", "B128", "B99999999999999999999999", "A", "A8+",
                             "A-1-", "H", "H_8", "H0_", "H4_8", "H0_9", "S", "b0"})
        CC_CHECK(CompileBinding(text, false, key).kind == BindingKind::None);
    CC_CASE("キーボード名を機器の設定と混同しない");
    CC_CHECK_EQ(CompileBinding("S", true, key).index, 83);
    CC_CHECK_EQ(CompileBinding("F1", true, key).index, 112); // 遮断は採取時の既存処理。
    CC_CHECK(CompileBinding("B0", true, key).kind == BindingKind::None);
    CC_CASE("再公開で変更・削除・機器種別変更を取り込み古い割当を残さない");
    CompiledInputBindings bindings;
    std::map<std::string, std::string> settings;
    auto read = [&](const char *name, const char *fallback) {
        auto it = settings.find(name);
        return it == settings.end() ? std::string(fallback) : it->second;
    };
    bindings.Reload(false, read, key);
    CC_CHECK(bindings[CompiledInputBindings::A].kind == BindingKind::Button);
    CC_CHECK_EQ(bindings[CompiledInputBindings::UpAlt].index, 1);
    settings["A"] = "B127";
    settings["Up_Alt"] = "";
    bindings.Reload(false, read, key);
    CC_CHECK_EQ(bindings[CompiledInputBindings::A].index, 127);
    CC_CHECK(bindings[CompiledInputBindings::UpAlt].kind == BindingKind::None);
    settings["A"] = "S";
    bindings.Reload(true, read, key);
    CC_CHECK(bindings[CompiledInputBindings::A].kind == BindingKind::Key);
    CC_CHECK_EQ(bindings[CompiledInputBindings::A].index, 83);
    CC_CHECK(bindings[CompiledInputBindings::Up].kind == BindingKind::None);
    settings["A"] = "";
    bindings.Reload(true, read, key);
    CC_CHECK(bindings[CompiledInputBindings::A].kind == BindingKind::None);
    return cccaster::test::Summarize("compiled_input_bindings");
}
