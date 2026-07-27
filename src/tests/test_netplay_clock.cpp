// ============================================================================
// test_netplay_clock.cpp — NetplayClock（θ推定・α補正）の特性化テスト
//
// 【目的】
//   NTP T1-T4 の計算、最小RTTフィルタ、α補正カーブを固定する。
//   ここが狂うとメトロノームの歩調が狂い、症状は「ロードでずれる」
//   「対戦中に片側だけ加速する」として現れるため、数値で押さえておく。
//
// 【依存】
//   NetplayClock.cpp は WasapiClock::GetTimeUs() だけを外部に要求する。
//   stub_wasapi_clock.cpp で置換しており、実時刻には一切依存しない。
// ============================================================================

#include "test_support.hpp"
#include "core_dll/sync/NetplayClock.hpp"

using cccaster::core::timer::NetplayClock;

// ============================================================================
// θ / RTT の計算
// ============================================================================

static void Ntp_ComputesRttAndThetaFromT1ToT4() {
    CC_CASE("NetplayClock: RTT=(T4-T1)-(T3-T2), θ=((T2-T1)+(T3-T4))/2");
    NetplayClock c;
    // 相手の時計が +1000us 進んでおり、片道 500us の理想ケース
    //   T1=0 送信 / T2=1500 相手受信 / T3=1600 相手送信 / T4=1100 自分受信
    //   RTT = (1100-0) - (1600-1500) = 1000
    //   θ   = ((1500-0) + (1600-1100)) / 2 = 1000
    c.AddNtpSample(0, 1500, 1600, 1100);

    CC_CHECK_EQ(c.GetRttUs(), 1000);
    CC_CHECK_EQ(c.GetThetaUs(), 1000);
}

static void Ntp_RejectsNegativeRttSamples() {
    CC_CASE("NetplayClock: RTT が負のサンプルは破棄する");
    NetplayClock c;
    c.AddNtpSample(0, 1500, 1600, 1100);   // 有効: RTT=1000
    c.AddNtpSample(0, 5000, 9000, 1000);   // RTT = 1000 - 4000 = -3000 → 破棄

    CC_CHECK_EQ(c.GetRttUs(), 1000);
    CC_CHECK_EQ(c.GetThetaUs(), 1000);
}

static void Ntp_AdoptsThetaOfMinimumRttSample() {
    CC_CASE("NetplayClock: 最小RTTサンプルのθを採用する");
    NetplayClock c;
    // 1本目: RTT=10000 の劣悪サンプル（θ=5000）
    c.AddNtpSample(0, 10000, 10000, 10000);
    CC_CHECK_EQ(c.GetRttUs(), 10000);
    CC_CHECK_EQ(c.GetThetaUs(), 5000);

    // 2本目: RTT=1000 の良サンプル（θ=1000）→ こちらを採用
    c.AddNtpSample(0, 1500, 1600, 1100);
    CC_CHECK_EQ(c.GetRttUs(), 1000);
    CC_CHECK_EQ(c.GetThetaUs(), 1000);

    // 3本目: 再び劣悪 → 最小RTTは維持される
    c.AddNtpSample(0, 20000, 20000, 20000);
    CC_CHECK_EQ(c.GetRttUs(), 1000);
    CC_CHECK_EQ(c.GetThetaUs(), 1000);
}

// ============================================================================
// θ安定判定（Counting へ移行する条件）
// ============================================================================

static void ThetaStable_RequiresMinimumSampleCount() {
    CC_CASE("NetplayClock: サンプル数が STABLE_MIN 未満なら不安定扱い");
    NetplayClock c;
    for (int i = 0; i < NetplayClock::STABLE_MIN - 1; i++) {
        c.AddNtpSample(0, 1500, 1600, 1100);
    }
    CC_CHECK(!c.IsThetaStable());

    c.AddNtpSample(0, 1500, 1600, 1100);
    CC_CHECK(c.IsThetaStable());
}

static void ThetaStable_FalseWhenSamplesScatter() {
    CC_CASE("NetplayClock: θのばらつきが大きいと不安定と判定する");
    NetplayClock c;
    // θ を 0 と 20000us で交互に振らせる（σ >> STABLE_SIGMA=1000）
    for (int i = 0; i < NetplayClock::STABLE_MIN + 2; i++) {
        if (i % 2 == 0) c.AddNtpSample(0, 500, 600, 1000);        // θ=50
        else            c.AddNtpSample(0, 20500, 20600, 1000);    // θ=20050
    }
    CC_CHECK(!c.IsThetaStable());
}

// ============================================================================
// α補正カーブ（3段階: デッドバンド → 二乗 → 飽和）
// ============================================================================

/// θ を狙った値にした上で baseline=0 の時計を作る。
/// T1=0, T4=2*owd とし、T2=T3=owd+theta にすると RTT=2*owd, θ=theta になる。
static void FeedTheta(NetplayClock& c, int64_t theta, int64_t owd = 500) {
    c.AddNtpSample(0, owd + theta, owd + theta, 2 * owd);
}

static void Alpha_NoCorrectionInsideDeadBand() {
    CC_CASE("NetplayClock: |Δθ| がデッドバンド内なら補正なし");
    NetplayClock c;
    for (int i = 0; i < 3; i++) FeedTheta(c, NetplayClock::DEAD_BAND_US - 1);

    CC_CHECK_EQ(c.GetTickUs(), NetplayClock::BASE_TICK_US);
}

static void Alpha_SaturatesBeyondStrongThreshold() {
    CC_CASE("NetplayClock: |Δθ| が飽和閾値を超えたら最大補正");
    NetplayClock c;
    // 相手が先行（Δθ>0）→ 自分を速く = ティックを短く
    for (int i = 0; i < 3; i++) FeedTheta(c, NetplayClock::STRONG_TH_US + 10000);

    CC_CHECK_EQ(c.GetTickUs(),
                NetplayClock::BASE_TICK_US - NetplayClock::MAX_ALPHA_US);
}

static void Alpha_SignFollowsDriftDirection() {
    CC_CASE("NetplayClock: Δθ の符号でティックの伸縮方向が決まる");
    NetplayClock c;
    // 相手が遅延（Δθ<0）→ 自分を遅く = ティックを長く
    for (int i = 0; i < 3; i++) FeedTheta(c, -(NetplayClock::STRONG_TH_US + 10000));

    CC_CHECK_EQ(c.GetTickUs(),
                NetplayClock::BASE_TICK_US + NetplayClock::MAX_ALPHA_US);
}

static void Alpha_UsesDeltaFromBaselineNotAbsoluteTheta() {
    CC_CASE("NetplayClock: α補正はベースラインθからの差分で決まる");
    NetplayClock c;
    // 大きな絶対θ（時計そのものがずれている）を積んでからベースライン確定
    for (int i = 0; i < 3; i++) FeedTheta(c, 100000);
    c.SetBaselineTheta();

    // 絶対θは 100000 のままだが Δθ=0 なので補正は入らない
    CC_CHECK_EQ(c.GetBaselineTheta(), 100000);
    CC_CHECK_EQ(c.GetTickUs(), NetplayClock::BASE_TICK_US);
}

static void Alpha_NoCorrectionWithTooFewSamples() {
    CC_CASE("NetplayClock: サンプル3本未満では補正しない");
    NetplayClock c;
    FeedTheta(c, NetplayClock::STRONG_TH_US * 2);
    FeedTheta(c, NetplayClock::STRONG_TH_US * 2);

    CC_CHECK_EQ(c.GetTickUs(), NetplayClock::BASE_TICK_US);
}

// ============================================================================
// スタート時刻の合意
// ============================================================================

static void AgreedStartTime_TakesLaterOfBothSides() {
    CC_CASE("NetplayClock: 合意開始時刻は両者の遅い方");
    NetplayClock c;   // θ=0（サンプルなし）
    c.SetLocalStartTime(5000);
    c.SetPeerStartTime(8000);

    CC_CHECK_EQ(c.GetAgreedStartTime(), 8000);
}

static void AgreedStartTime_ZeroUntilBothSidesKnown() {
    CC_CASE("NetplayClock: 片側だけでは合意時刻を返さない");
    NetplayClock c;
    CC_CHECK_EQ(c.GetAgreedStartTime(), 0);

    c.SetLocalStartTime(5000);
    CC_CHECK_EQ(c.GetAgreedStartTime(), 0);
}

static void PeerStartTime_IsConvertedToLocalClockByTheta() {
    CC_CASE("NetplayClock: 相手の開始時刻はθでローカル基準に変換される");
    NetplayClock c;
    c.AddNtpSample(0, 1500, 1600, 1100);   // θ=1000
    c.SetLocalStartTime(5000);
    c.SetPeerStartTime(8000);              // → 8000 - 1000 = 7000

    CC_CHECK_EQ(c.GetAgreedStartTime(), 7000);
}

static void PeerStartTime_ConversionUsesThetaAtCallTime() {
    CC_CASE("[HAZARD] NetplayClock: θ変換は SetPeerStartTime 実行時の値で固定される");
    // 後からθが更新されても、変換済みの _peerStartTimeUs は再計算されない。
    // START 合意の前後でθが動くと、開始時刻が片側だけずれる。
    NetplayClock c;
    c.AddNtpSample(0, 1500, 1600, 1100);   // RTT=1000, θ=1000
    c.SetLocalStartTime(1000);
    c.SetPeerStartTime(8000);              // → 8000 - 1000 = 7000 で確定

    c.AddNtpSample(0, 300, 300, 400);      // RTT=400 (より良い), θ=100 に更新
    CC_CHECK_EQ(c.GetThetaUs(), 100);
    CC_CHECK_EQ(c.GetAgreedStartTime(), 7000);   // 7900 に再計算はされない
}

static void MinRtt_TieKeepsEarlierSample() {
    CC_CASE("[HAZARD] NetplayClock: RTT 同値ではθを新しいサンプルに乗り換えない");
    // 最小RTT探索が strict less-than のため、RTT が安定した回線では
    // 最初に記録されたθを掴んだまま更新されなくなる。
    NetplayClock c;
    c.AddNtpSample(0, 1500, 1600, 1100);   // RTT=1000, θ=1000
    c.AddNtpSample(0, 600, 600, 1000);     // RTT=1000（同値）, θ=100

    CC_CHECK_EQ(c.GetRttUs(), 1000);
    CC_CHECK_EQ(c.GetThetaUs(), 1000);     // 100 にはならない
}

static void StartTime_ZeroIsTreatedAsUnset() {
    CC_CASE("[HAZARD] NetplayClock: 開始時刻 0 は「未設定」と区別できない");
    // ミスマッチフレームと同じセンチネル 0 の問題。
    // 実運用では WASAPI 時刻が 0 にならないため顕在化しないが、
    // テストやリプレイで時刻を 0 起点にすると合意が成立しない。
    NetplayClock c;
    c.SetLocalStartTime(0);
    c.SetPeerStartTime(8000);

    CC_CHECK_EQ(c.GetAgreedStartTime(), 0);
}

// ============================================================================
// リセット
// ============================================================================

static void Reset_ClearsSamplesButKeepsBaselineTheta() {
    CC_CASE("[HAZARD] NetplayClock: Reset は baselineTheta を消さない");
    // Reset() 後に GetTickUs() を呼ぶと、θ=0 と古い baseline の差分で
    // 補正が入りうる。セッション再開時は SetBaselineTheta() の呼び直しが要る。
    NetplayClock c;
    for (int i = 0; i < 3; i++) FeedTheta(c, 5000);
    c.SetBaselineTheta();
    CC_CHECK_EQ(c.GetBaselineTheta(), 5000);

    c.Reset();

    CC_CHECK_EQ(c.GetThetaUs(), 0);
    CC_CHECK_EQ(c.GetBaselineTheta(), 5000);   // 残る
    CC_CHECK_EQ(c.GetAgreedStartTime(), 0);
}

// ============================================================================

int main() {
    Ntp_ComputesRttAndThetaFromT1ToT4();
    Ntp_RejectsNegativeRttSamples();
    Ntp_AdoptsThetaOfMinimumRttSample();

    ThetaStable_RequiresMinimumSampleCount();
    ThetaStable_FalseWhenSamplesScatter();

    Alpha_NoCorrectionInsideDeadBand();
    Alpha_SaturatesBeyondStrongThreshold();
    Alpha_SignFollowsDriftDirection();
    Alpha_UsesDeltaFromBaselineNotAbsoluteTheta();
    Alpha_NoCorrectionWithTooFewSamples();

    AgreedStartTime_TakesLaterOfBothSides();
    AgreedStartTime_ZeroUntilBothSidesKnown();
    PeerStartTime_IsConvertedToLocalClockByTheta();
    PeerStartTime_ConversionUsesThetaAtCallTime();
    MinRtt_TieKeepsEarlierSample();
    StartTime_ZeroIsTreatedAsUnset();

    Reset_ClearsSamplesButKeepsBaselineTheta();

    return cccaster::test::Summarize("netplay_clock");
}
