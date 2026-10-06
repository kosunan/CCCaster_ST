#include "core_dll/engine/TrainingCharacterSelection.hpp"
#include "core_dll/sync/SelectionState.hpp"
#include "test_support.hpp"
#include <set>
using namespace cccaster::training_character;
int main() {
    CC_CASE("通常選択可能な全キャラとムーンを決定まで保持する");
    std::set<uint32_t> unique;
    for (auto character : Characters) {
        CC_CHECK(cccaster::core::sync::SelectionState::CharacterCell(CursorCharacter(character)) >= 0);
        CC_CHECK(unique.insert(character).second);
        for (unsigned slot=0; slot<MoonCount; ++slot) {
            const auto moon = MoonValue(character,slot);
            Selection s; s.Open({Choice{character,moon},Choice{11,0}},0);
            CC_CHECK_EQ(s.choice.character,character); CC_CHECK_EQ(s.choice.moon,moon);
            CC_CHECK(s.Step(Action::Accept)==Result::None && s.open && s.field==Field::Moon);
            CC_CHECK(s.Step(Action::Accept)==Result::None && s.open && s.field==Field::Confirm);
            CC_CHECK(s.Step(Action::Accept)==Result::Apply && !s.open);
            CC_CHECK(s.Step(Action::Accept)==Result::None);
        }
    }
    CC_CASE("キャンセル・確定前の戻り・P2選択が元の確定値を書き換えない");
    Selection s; s.Open({Choice{22,0},Choice{11,2}},0);
    s.Step(Action::Right); s.Step(Action::Accept); s.Step(Action::Right);
    s.Step(Action::Accept);
    CC_CHECK(s.Step(Action::Cancel)==Result::None && s.field==Field::Moon);
    CC_CHECK(s.Step(Action::Cancel)==Result::None && s.field==Field::Character);
    CC_CHECK(s.Step(Action::Cancel)==Result::Cancelled && !s.open);
    CC_CHECK_EQ(s.original[0].character,22); CC_CHECK_EQ(s.original[0].moon,0);
    s.Open({Choice{22,0},Choice{11,2}},0);
    s.Step(Action::Up); s.Step(Action::Right);
    CC_CHECK(s.field==Field::Player); CC_CHECK_EQ(s.player,1);
    CC_CHECK_EQ(s.choice.character,11); CC_CHECK_EQ(s.choice.moon,2);
    s.Step(Action::Accept); s.Step(Action::Left); s.Step(Action::Accept);
    s.Step(Action::Right);
    CC_CHECK_EQ(s.choice.moon,9); CC_CHECK_EQ(s.original[1].moon,2);
    CC_CASE("端の移動でも無効なキャラやムーンへ出ない");
    s.Open({Choice{22,0},Choice{11,0}},0);
    s.Step(Action::Left); CC_CHECK_EQ(s.choice.character,Characters.back());
    s.Step(Action::Down); CC_CHECK(s.field==Field::Moon);
    s.Step(Action::Left); CC_CHECK_EQ(s.choice.moon,9);
    for (int i=0;i<400;++i) {
        s.Step(Action::Up); s.Step(Action::Right); s.Step(Action::Down);
        CC_CHECK(s.index<Characters.size() && (s.choice.moon<3 || s.choice.moon==8 || s.choice.moon==9) && s.player<2);
    }
    CC_CASE("ボス専用ムーンへの補正と未収録スタイルのスキップ");
    s.available.fill(7);
    s.available[31]=8; // G_AKIHAは9のみ。
    s.available[32]=1; // HERMESは0のみ。
    s.available[33]=8; // B_AKIHAは8のみ。
    s.Open({Choice{53,0},Choice{16,9}},0);
    CC_CHECK_EQ(s.choice.moon,8);
    s.Step(Action::Accept); s.Step(Action::Right);
    CC_CHECK_EQ(s.choice.moon,8);
    s.Step(Action::Up); s.Step(Action::Left);
    CC_CHECK_EQ(s.choice.character,32); CC_CHECK_EQ(s.choice.moon,0);
    s.Open({Choice{22,2},Choice{16,9}},0);
    s.Step(Action::Accept); s.Step(Action::Right);
    CC_CHECK_EQ(s.choice.moon,0);
    s.Open({Choice{22,0},Choice{16,9}},1);
    CC_CHECK_EQ(s.choice.moon,9);
    return cccaster::test::Summarize("Trainingキャラ選択");
}
