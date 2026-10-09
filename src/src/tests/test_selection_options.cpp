#include "core_dll/engine/SelectionOptions.hpp"
#include "core_dll/ui/HudDisplay.hpp"
#include <cstdlib>
#include <iostream>
using namespace cccaster::domain::scene::selection_options;
using cccaster::game_interface::GameInput;
void check(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    Menu menu;
    const auto step = [&](GameInput input = {}, unsigned action = 0, bool available = true,
                          bool mapping = false, bool editable = true) {
        return menu.Step(input,action,available,mapping,editable,true);
    };
    check(!step({6,CC_BUTTON_A}).block,"通常操作を維持");
    check(step({0,CC_BUTTON_START}).block && menu.open,"STARTで開きゲームへ渡さない");
    check(step({0,CC_BUTTON_START}).block && menu.open,"長押しで連続開閉しない");
    step();
    check(step({6,0}).delayStep == 1,"右で増加");
    check(step({6,0}).delayStep == 0,"長押しで連続変更しない");
    step();
    check(step({4,0},0,true,false,false).delayStep == 0,"確定後は変更不可");
    check(step({},Down).block && menu.row == 1,"下で背景行");
    check(step({},Left).animation == 0,"左でOFF");
    check(step({},Right).animation == 1,"右でON");
    check(step({0,CC_BUTTON_A}).animation == 0,"Aで背景切替");
    step({},Down);
    check(menu.row == 2,"下でHUD行");
    const auto hud = step({6,0},0,true,false,false);
    check(hud.block && hud.hudStep == 1 && !hud.delayStep && hud.animation == -1,"D変更不可でもHUDだけを変更");
    check(!step({6,0}).hudStep,"HUDも長押しで連続変更しない");
    check(step({},Left).hudStep == -1,"左でHUDを逆順へ");
    check(step({0,CC_BUTTON_A}).hudStep == 1,"AでHUD切替");
    step({},Down);
    check(menu.row == 3,"下で解像度行");
    const auto resize = step({},Right);
    check(resize.block && resize.resolutionStep == 1 && resize.fullscreen == -1 && !resize.hudStep && !resize.delayStep,"解像度のみを変更");
    check(step({},Left).resolutionStep == -1,"左で小さい解像度へ");
    step({},Down);
    check(menu.row == 4 && step({},Right).fullscreen == 1,"全画面をONへ");
    check(step({},Left).fullscreen == 0,"全画面をOFFへ");
    check(menu.Step({0,CC_BUTTON_A},0,true,false,true,true,true).fullscreen == 0,"Aで全画面を解除");
    for (unsigned i = 0; i < 4; ++i) {
        step({},Down);
        check(menu.row == i+5,"標準の表示設定行を選択");
        const auto result = step({},Right);
        check(result.nativeStep == 1 && !result.resolutionStep && result.fullscreen == -1 && !result.delayStep,"標準表示設定だけを操作");
        const auto option = static_cast<cccaster::game_interface::NativeDisplayOption>(i);
        const auto* definition = cccaster::game_interface::DisplayDefinition(option);
        check(cccaster::game_interface::NextDisplayValue(option,0,-1) == definition->count-1,"標準選択肢の逆順循環");
        check(cccaster::game_interface::NextDisplayValue(option,definition->count-1,1) == 0,"標準選択肢の順方向循環");
        check(cccaster::game_interface::NextDisplayValue(option,-1,1) == -1,"未対応の値を操作しない");
    }
    step({},Down);
    check(menu.row == 0,"最下段から先頭へ");
    step({},Up);
    check(menu.row == 8,"先頭から最下段へ");
    using cccaster::domain::ui::HudDisplay;
    using cccaster::domain::ui::HudDisplayMode;
    HudDisplay::Cycle(-1);
    check(HudDisplay::Get() == HudDisplayMode::Hidden,"通常から左で非表示");
    HudDisplay::Cycle();
    check(HudDisplay::Get() == HudDisplayMode::Compact,"非表示から右で通常");
    HudDisplay::Cycle();
    check(HudDisplay::Get() == HudDisplayMode::Detailed,"通常から右で詳細");
    HudDisplay::Cycle(-1);
    check(step({0,CC_BUTTON_B}).block && !menu.open,"Bで閉じる");
    check(step({0,CC_BUTTON_A}).block,"閉じた後も全解放まで遮断");
    check(step().block && !step({0,CC_BUTTON_A}).block,"全解放後に通常操作へ戻る");
    step({},Toggle);
    check(menu.open,"F1で再び開く");
    check(step({0,CC_BUTTON_START},0,true,true).block && !menu.open,"F4と排他的");
    step();
    step({},Toggle);
    check(step({0,CC_BUTTON_A},0,false).block && !menu.open,"シーン遷移で閉じる");
    check(!step({},Toggle,false).delayStep && !menu.open,"対象外シーンで開かない");
    std::cout << "selection options passed\n";
}
