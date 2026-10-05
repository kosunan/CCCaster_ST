#include "test_support.hpp"
#include "core_dll/sync/SettingsCommands.hpp"
#include "core_dll/sync/SelectionState.hpp"
using S = cccaster::core::sync::SettingsCommands;
int main() {
    CC_CASE("設定操作は入力へ一度だけ載り、確定するまで次の要求を待つ");
    S::Reset(2, 4);
    CC_CHECK(S::Request(false, 3));
    CC_CHECK(!S::Request(true, 2));
    auto command = S::Capture();
    CC_CHECK(command != 0);
    CC_CHECK_EQ(S::Capture(), 0u);
    CC_CHECK(S::pending.load() != 0);
    CC_CHECK(S::Apply(command | 0x00060010u, true));
    CC_CHECK_EQ(S::delay.load(), 3);
    CC_CHECK_EQ(S::pending.load(), 0u);
    CC_CHECK_EQ((command | 0x00060010u) & S::GameMask, 0x00060010u);
    CC_CASE("合計8超でも採用し、P1とP2の同時操作を一定順に処理する");
    CC_CHECK(S::Request(true, 7));
    S::Apply(S::Capture(), true);
    CC_CHECK_EQ(S::rollback.load(), 7);
    S::Apply(0xa4000000u, false);
    S::Apply(0xb2000000u, true);
    CC_CHECK_EQ(S::delay.load(), 4);
    CC_CHECK_EQ(S::rollback.load(), 2);
    CC_CHECK(!S::Request(false, 9));
    CC_CASE("D0〜8/R0〜7を桁落ちさせず、設定と選択状態の双方で受理する");
    for (int d = 0; d <= 8; ++d) for (int r = 0; r <= 7; ++r) {
        S::Reset(2, 4);
        CC_CHECK(S::Request(false, d)); S::Apply(S::Capture(), true);
        CC_CHECK(S::Request(true, r));
        const auto rollbackCommand = S::Capture();
        CC_CHECK(S::Apply(rollbackCommand | 0x00060010u, true));
        CC_CHECK_EQ(S::delay.load(), d); CC_CHECK_EQ(S::rollback.load(), r);
        CC_CHECK_EQ(S::pending.load(), 0u);
        cccaster::core::sync::SelectionState local, peer;
        local.epoch = 65536; local.revision = 1; local.delay = d; local.rollback = r;
        local.command = rollbackCommand;
        CC_CHECK(peer.Accept(local));
        CC_CHECK_EQ(peer.delay, unsigned(d)); CC_CHECK_EQ(peer.rollback, unsigned(r));
    }
    CC_CASE("各上限と負数は引き続き拒否し、直前の有効設定を保持する");
    CC_CHECK(!S::Request(false, -1)); CC_CHECK(!S::Request(true, 8));
    S::Apply(0xa9000000u, false); S::Apply(0xb8000000u, false);
    CC_CHECK_EQ(S::delay.load(), 8); CC_CHECK_EQ(S::rollback.load(), 7);
    CC_CHECK(!cccaster::public_api::NetplaySettings::IsValid(-1, 0));
    CC_CHECK(!cccaster::public_api::NetplaySettings::IsValid(0, -1));
    CC_CHECK(!cccaster::public_api::NetplaySettings::IsValid(9, 0));
    CC_CHECK(!cccaster::public_api::NetplaySettings::IsValid(0, 8));
    CC_CHECK(cccaster::public_api::NetplaySettings::DefaultRollback == 7);
    CC_CHECK(S::ValidCommand(0xb7000000u));
    CC_CHECK(!S::ValidCommand(0xb7000001u));
    CC_CASE("境界まで届かなかった操作を持ち越さない");
    S::Request(false, 1);
    S::Boundary();
    CC_CHECK_EQ(S::Capture(), 0u);
    CC_CHECK_EQ(S::pending.load(), 0u);
    return cccaster::test::Summarize("settings_commands");
}
