// ============================================================================
// test_input_buffers.cpp — MatchInputBuffer / MenuInputBuffer の特性化テスト
//
// 【目的】
//   入力パイプライン再構築の前に、両バッファの「現在の実際の挙動」を固定する。
//   仕様として正しいと確認できたものと、危険な挙動をそのまま記録したものが
//   混在する。後者は [HAZARD] で明示してある — テストが緑であることは
//   「正しい」ではなく「変わっていない」を意味する。
//
//   [HAZARD] のテストは、その挙動を意図的に変更する時に失敗する。
//   失敗したらテストを直すのではなく、変更が意図通りかを判断すること。
//
// 【依存】
//   両クラスはヘッダオンリーで依存ゼロ。ゲーム・DLL・通信を一切必要としない。
// ============================================================================

#include "test_support.hpp"
#include "core_dll/sync/MatchInputBuffer.hpp"
#include "core_dll/sync/MenuInputBuffer.hpp"

using cccaster::core::sync::MatchInputBuffer;
using cccaster::core::sync::MenuInputBuffer;

// ============================================================================
// MatchInputBuffer — 読取位置の算出
// ============================================================================

static void ReadPos_SubtractsDelayPlusRollback() {
    CC_CASE("MatchInputBuffer: readPos = writeHead - (delay + maxRollback)");
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(/*startFrame*/ 100, /*delay*/ 2, /*maxRollback*/ 4);

    CC_CHECK_EQ(b.GetWriteHead(), 100u);
    CC_CHECK_EQ(b.GetReadPos(), 94u);
}

static void ReadPos_ClampsOffsetToAtLeastOne() {
    CC_CASE("MatchInputBuffer: delay=0 rollback=0 でも最低1F遅れる");
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(100, 0, 0);

    CC_CHECK_EQ(b.GetReadPos(), 99u);
}

static void ReadPos_ClampsToZeroNearSessionStart() {
    CC_CASE("MatchInputBuffer: writeHead が offset 未満なら readPos=0");
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(3, 2, 4);  // offset=6 > wh=3

    CC_CHECK_EQ(b.GetReadPos(), 0u);
}

static void ReadFrameForGame_TreatsFrameZeroAsInvalid() {
    CC_CASE("[HAZARD] MatchInputBuffer: frame 0 は永久に読み出せない");
    // GetReadPos()==0 は「セッション開始直後で読めない」と
    // 「正当なフレーム0」の両方を表す。区別できないため frame 0 は
    // 確定済みでも配信されない。
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(1, 0, 0);  // offset=1 → readPos = 0
    b.WriteSlot(0, false, 0x1111, 0x2222, /*confirmed*/ true);

    uint32_t p1 = 0xDEAD, p2 = 0xBEEF;
    CC_CHECK(!b.ReadFrameForGame(true, p1, p2));
    CC_CHECK_EQ(p1, 0xDEADu);  // 出力は書き換えられない
}

static void ReadFrameForGame_SwapsSidesByHostRole() {
    CC_CASE("MatchInputBuffer: isHost で P1/P2 が入れ替わる");
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(100, 2, 4);  // readPos = 94
    b.WriteSlot(94, false, /*local*/ 0xAAAA, /*remote*/ 0xBBBB, /*confirmed*/ true);

    uint32_t p1 = 0, p2 = 0;
    CC_CHECK(b.ReadFrameForGame(/*isHost*/ true, p1, p2));
    CC_CHECK_EQ(p1, 0xAAAAu);
    CC_CHECK_EQ(p2, 0xBBBBu);

    CC_CHECK(b.ReadFrameForGame(/*isHost*/ false, p1, p2));
    CC_CHECK_EQ(p1, 0xBBBBu);
    CC_CHECK_EQ(p2, 0xAAAAu);
}

static void ReadFrameForGame_RequiresConfirmedSlot() {
    CC_CASE("MatchInputBuffer: 未確定スロットは読み出さない");
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(100, 2, 4);
    b.WriteSlot(94, false, 0xAAAA, 0xBBBB, /*confirmed*/ false);

    uint32_t p1 = 0, p2 = 0;
    CC_CHECK(!b.ReadFrameForGame(true, p1, p2));
}

// ============================================================================
// MatchInputBuffer — ミスマッチ検出（ロールバック判定の入口）
// ============================================================================

static void ConfirmRemote_RecordsOldestMismatch() {
    CC_CASE("MatchInputBuffer: ミスマッチは最も古いフレームが残る");
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(0, 2, 4);
    b.WriteSlot(20, false, 0, /*予測*/ 0xAA, /*confirmed*/ false);
    b.WriteSlot(10, false, 0, /*予測*/ 0xBB, /*confirmed*/ false);

    b.ConfirmRemote(20, 0xCC);   // 予測外れ → 20
    b.ConfirmRemote(10, 0xDD);   // 予測外れ → より古い 10 を採用

    CC_CHECK_EQ(b.ConsumeMismatch(), 10u);
    CC_CHECK_EQ(b.ConsumeMismatch(), 0u);  // 消費後はクリアされる
}

static void ConfirmRemote_AdvancesConfirmedFrameMonotonically() {
    CC_CASE("MatchInputBuffer: confirmedRemoteFrame は後退しない");
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(0, 2, 4);

    b.ConfirmRemote(20, 0x01);
    CC_CHECK_EQ(b.GetConfirmedRemoteFrame(), 20u);
    b.ConfirmRemote(10, 0x02);   // 冗長入力による過去フレームの再確定
    CC_CHECK_EQ(b.GetConfirmedRemoteFrame(), 20u);
}

static void ConfirmRemote_SkipsMismatchOnAlreadyConfirmedSlot() {
    CC_CASE("[HAZARD] MatchInputBuffer: 確定済みスロットは値が変わっても検出しない");
    // 冗長入力(最大10F)は毎パケット再送されるため、同じフレームが複数回
    // ConfirmRemote される。2回目以降は confirmed==true なので、値が
    // 食い違ってもミスマッチとして扱われない = デシンクが黙って通過する。
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(0, 2, 4);
    b.WriteSlot(30, false, 0, 0xAA, /*confirmed*/ true);

    b.ConfirmRemote(30, 0xFF);   // 確定済みの値と異なる

    CC_CHECK_EQ(b.ConsumeMismatch(), 0u);
    CC_CHECK_EQ(b.GetSlot(30).remoteInput, 0xFFu);  // 値は黙って上書きされる
}

static void MismatchAtFrameZero_IsIndistinguishableFromNone() {
    CC_CASE("[HAZARD] MatchInputBuffer: frame 0 のミスマッチは「なし」と区別できない");
    // _mismatchFrame の「なし」を 0 で表しているため。
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(0, 2, 4);
    b.WriteSlot(0, false, 0, 0xAA, /*confirmed*/ false);

    b.ConfirmRemote(0, 0xBB);   // 実際にはミスマッチ

    CC_CHECK_EQ(b.ConsumeMismatch(), 0u);
}

// ============================================================================
// MatchInputBuffer — リングバッファ境界
// ============================================================================

static void Ring_WrapsSilentlyWithoutFrameValidation() {
    CC_CASE("[HAZARD] MatchInputBuffer: RING_SIZE 周回で別フレームを黙って返す");
    // GetSlot() / ReadFrameForGame() は slot.frame == 要求フレーム を検証しない。
    // 600F(10秒)以上の進みが起きると、古いフレームの読み出しが
    // 新しいフレームのデータを返す。
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(0, 2, 4);

    b.WriteSlot(10, false, 0x1111, 0x2222, true);
    b.WriteSlot(10 + MatchInputBuffer::RING_SIZE, false, 0x3333, 0x4444, true);

    const auto& slot = b.GetSlot(10);
    CC_CHECK_EQ(slot.localInput, 0x3333u);   // frame 10 を要求したのに 610 のデータ
    CC_CHECK_EQ(slot.frame, 610u);           // frame フィールドだけが食い違いを示す
}

static void ConfirmRemote_LeavesSlotFrameFieldStale() {
    CC_CASE("[HAZARD] MatchInputBuffer: ConfirmRemote は slot.frame を更新しない");
    // WriteSlot される前に相手入力が届いた場合、スロットの frame は
    // 前の周回の値のまま残る。frame による検証を後から入れる際の前提になる。
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(0, 2, 4);

    b.ConfirmRemote(50, 0x99);   // WriteSlot なしで確定だけ来る

    const auto& slot = b.GetSlot(50);
    CC_CHECK_EQ(slot.remoteInput, 0x99u);
    CC_CHECK(slot.confirmed);
    CC_CHECK_EQ(slot.frame, 0u);   // Reset 直後の 0 のまま
}

// ============================================================================
// MatchInputBuffer — 進行可能フレームと状態クリア
// ============================================================================

static void EffectiveHead_IsCappedByPeerConfirmation() {
    CC_CASE("MatchInputBuffer: effectiveHead は相手の確定フレームで頭打ちになる");
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(100, 2, 4);   // wh=100, offset=6, confirmed=100

    CC_CHECK_EQ(b.GetEffectiveHead(), 94u);   // min(94, 100)

    b.SetWriteHead(200);                       // 自分だけ先行
    CC_CHECK_EQ(b.GetEffectiveHead(), 100u);   // min(194, 100) → 相手待ち
}

static void Reset_KeepsSessionParamsButClearsProgress() {
    CC_CASE("MatchInputBuffer: Reset は D/R を残し、進行状態だけ消す");
    auto& b = MatchInputBuffer::GetInstance();
    b.Initialize(100, 3, 5);
    b.WriteSlot(94, false, 0x1234, 0x5678, true);

    b.Reset();

    CC_CHECK_EQ(b.GetDelay(), 3);
    CC_CHECK_EQ(b.GetMaxRollback(), 5);
    CC_CHECK_EQ(b.GetWriteHead(), 0u);
    CC_CHECK_EQ(b.GetConfirmedRemoteFrame(), 0u);
    CC_CHECK_EQ(b.GetSlot(94).localInput, 0u);
}

static void Singleton_SharesStateAcrossCallSites() {
    CC_CASE("[HAZARD] MatchInputBuffer: シングルトンなので状態がフェーズ間で残る");
    // Reset() を呼ぶ責任が呼び出し側にある。フェーズ遷移で呼び忘れると
    // 前のラウンドの入力が次のラウンドに漏れる。
    auto& a = MatchInputBuffer::GetInstance();
    a.Initialize(100, 2, 4);
    a.WriteSlot(94, false, 0x1234, 0x5678, true);

    auto& b = MatchInputBuffer::GetInstance();
    CC_CHECK(&a == &b);
    CC_CHECK_EQ(b.GetSlot(94).localInput, 0x1234u);
}

// ============================================================================
// MenuInputBuffer — キャラセレ用（ロールバックなし）
// ============================================================================

static void Menu_ReadPos_UsesDelayOnly() {
    CC_CASE("MenuInputBuffer: readPos = writeHead - delay（rollback を含まない）");
    auto& m = MenuInputBuffer::GetInstance();
    m.Initialize(100, /*delay*/ 2);

    CC_CHECK_EQ(m.GetReadPos(), 98u);
}

static void Menu_ReadPos_ClampsOffsetToAtLeastOne() {
    CC_CASE("MenuInputBuffer: delay=0 でも最低1F遅れる");
    auto& m = MenuInputBuffer::GetInstance();
    m.Initialize(100, 0);

    CC_CHECK_EQ(m.GetReadPos(), 99u);
}

static void Menu_ReadFrameForGame_SwapsSidesByHostRole() {
    CC_CASE("MenuInputBuffer: isHost で P1/P2 が入れ替わる");
    auto& m = MenuInputBuffer::GetInstance();
    m.Initialize(100, 2);   // readPos = 98
    m.WriteSlot(98, /*local*/ 0x0F0F, /*remote*/ 0xF0F0, /*confirmed*/ true);

    uint32_t p1 = 0, p2 = 0;
    CC_CHECK(m.ReadFrameForGame(true, p1, p2));
    CC_CHECK_EQ(p1, 0x0F0Fu);
    CC_CHECK_EQ(p2, 0xF0F0u);
}

static void Menu_HasNoMismatchDetection() {
    CC_CASE("MenuInputBuffer: 予測外れを検出する仕組みがない");
    // キャラセレはロールバックしない前提のため、値が食い違っても
    // 上書きされるだけ。キャラセレのズレは通信ではなく
    // 入力フィルタ側（SceneInputFilter）で防ぐ設計になる。
    auto& m = MenuInputBuffer::GetInstance();
    m.Initialize(0, 2);
    m.WriteSlot(10, 0, /*予測*/ 0xAA, /*confirmed*/ false);

    m.ConfirmRemote(10, 0xBB);

    CC_CHECK_EQ(m.GetSlot(10).remoteInput, 0xBBu);
    CC_CHECK(m.GetSlot(10).confirmed);
}

static void Menu_Reset_KeepsDelay() {
    CC_CASE("MenuInputBuffer: Reset は delay を残す");
    auto& m = MenuInputBuffer::GetInstance();
    m.Initialize(100, 3);

    m.Reset();

    CC_CHECK_EQ(m.GetDelay(), 3);
    CC_CHECK_EQ(m.GetWriteHead(), 0u);
}

// ============================================================================

int main() {
    ReadPos_SubtractsDelayPlusRollback();
    ReadPos_ClampsOffsetToAtLeastOne();
    ReadPos_ClampsToZeroNearSessionStart();
    ReadFrameForGame_TreatsFrameZeroAsInvalid();
    ReadFrameForGame_SwapsSidesByHostRole();
    ReadFrameForGame_RequiresConfirmedSlot();

    ConfirmRemote_RecordsOldestMismatch();
    ConfirmRemote_AdvancesConfirmedFrameMonotonically();
    ConfirmRemote_SkipsMismatchOnAlreadyConfirmedSlot();
    MismatchAtFrameZero_IsIndistinguishableFromNone();

    Ring_WrapsSilentlyWithoutFrameValidation();
    ConfirmRemote_LeavesSlotFrameFieldStale();

    EffectiveHead_IsCappedByPeerConfirmation();
    Reset_KeepsSessionParamsButClearsProgress();
    Singleton_SharesStateAcrossCallSites();

    Menu_ReadPos_UsesDelayOnly();
    Menu_ReadPos_ClampsOffsetToAtLeastOne();
    Menu_ReadFrameForGame_SwapsSidesByHostRole();
    Menu_HasNoMismatchDetection();
    Menu_Reset_KeepsDelay();

    return cccaster::test::Summarize("input_buffers");
}
