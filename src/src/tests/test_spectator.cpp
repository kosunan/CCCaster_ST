#include "core_dll/spectator/Transport.hpp"
#include "core_dll/spectator/PlaybackPacing.hpp"
#include "core_dll/spectator/IntroPreview.hpp"
#include "cli_launcher/network_wrapper/SpectatorEndpoint.hpp"
#include <chrono>
#include <iostream>
#include <cstdarg>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <asio.hpp>
void HookLog(const char *) {}
using namespace cccaster::spectator;
static void Check(bool ok) { if (!ok) { std::cerr << "観戦検査失敗\n"; std::abort(); } }
template<class F> bool Until(F f) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    do { if (f()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    while (std::chrono::steady_clock::now() < end);
    return false;
}
int main() {
    struct PreviewMemory : cccaster::game_interface::IGameMemory {
        std::array<uint32_t, 4> state{7, 11, 13, 17}; // 進行・乱数・記録位置・入力を模擬。
        bool muted = false, saveOk = true, loadOk = true, previewOk = true;
        uint8_t intro = 2;
        bool IsAvailable() const override { return true; }
        uint32_t GameMode() const override { return 1; }
        uint8_t IntroState() const override { return intro; }
        uint32_t WorldTimer() const override { return state[0]; }
        uint32_t RealTimer() const override { return 0; }
        uint32_t MenuStateCounter() const override { return 0; }
        void WriteInput(cccaster::game_interface::GameInput p1, cccaster::game_interface::GameInput p2) override {
            state[3] = p1.Pack() | p2.Pack();
        }
        size_t SnapshotSize() const override { return sizeof(state); }
        bool SaveSnapshot(std::span<char> bytes) override {
            std::memcpy(bytes.data(), state.data(), sizeof(state)); return saveOk;
        }
        bool LoadSnapshot(std::span<char> bytes) override {
            std::memcpy(state.data(), bytes.data(), sizeof(state)); return loadOk;
        }
        bool SetIntroPreview(bool active) override {
            if (active && !previewOk) return false;
            muted = active; return true;
        }
    } previewMem;
    IntroPreview preview;
    const auto originalState = previewMem.state;
    const auto rounding = std::fegetround();
    std::fesetround(FE_DOWNWARD);
    Check(preview.Begin(previewMem) && preview.Pending() && previewMem.muted && previewMem.state[3] == 0);
    Check(!preview.Begin(previewMem));
    previewMem.state = {101, 103, 107, 109}; // 表示用更新で変わっても確定再生へ持ち込まない。
    std::fesetround(FE_UPWARD);
    Check(preview.Restore(previewMem) && !preview.Pending() && !previewMem.muted);
    Check(previewMem.state == originalState && std::fegetround() == FE_DOWNWARD);
    std::fesetround(rounding);
    Check(!preview.Restore(previewMem));
    previewMem.intro = 0; Check(!preview.Begin(previewMem));
    previewMem.intro = 2; previewMem.saveOk = false;
    Check(!preview.Begin(previewMem) && !preview.Pending() && !previewMem.muted);
    previewMem.saveOk = true; previewMem.previewOk = false;
    Check(!preview.Begin(previewMem) && !preview.Pending() && !previewMem.muted);
    previewMem.previewOk = true; Check(preview.Begin(previewMem)); previewMem.loadOk = false;
    Check(!preview.Restore(previewMem) && !preview.Pending() && !previewMem.muted);
    using Phase = cccaster::game_interface::GamePhase;
    Check(IsIntroPlayback(Phase::InGame, 1));
    Check(IsIntroPlayback(Phase::InGame, 2));
    Check(!IsIntroPlayback(Phase::InGame, 0));
    Check(!IsIntroPlayback(Phase::InGame, 3));
    Check(!IsIntroPlayback(Phase::Rematch, 2));
    Check(!IsIntroPlayback(Phase::CharaSelect, 2));
    // 現在/1試合前は描画ON、2試合以上前だけOFF。追いつき済み・古い通知はON。
    Check(!SkipCatchupDrawing(true, 7, 7));
    Check(!SkipCatchupDrawing(true, 7, 8));
    Check(SkipCatchupDrawing(true, 7, 9));
    Check(SkipCatchupDrawing(true, 7, 100));
    Check(!SkipCatchupDrawing(true, 8, 9));
    Check(!SkipCatchupDrawing(false, 7, 9));
    Check(!SkipCatchupDrawing(true, 7, 0));
    Check(!SkipCatchupDrawing(true, UINT32_MAX, 0));
    // ロード中の押下/解放を作り、イントロ・通常対戦・メニューへ追加操作を漏らさない。
    for (uint32_t tick = 1; tick <= 24; ++tick) {
        Check(LoadingConfirm(Phase::Loading, tick).buttons ==
            (tick % 8 == 1 ? CC_BUTTON_A | CC_BUTTON_CONFIRM : 0));
        Check(LoadingConfirm(Phase::InGame, tick).Pack() == 0);
        Check(LoadingConfirm(Phase::CharaSelect, tick).Pack() == 0);
        Check(LoadingConfirm(Phase::Rematch, tick).Pack() == 0);
    }
    Record a, b;
    a.Set(Input, 65537, std::array<uint32_t, 2>{0, 0x90010}); Check(a.Valid());
    a.size = 19; Check(!a.Valid()); a.size = 20;
    a.payload[1] = 0x100000; Check(!a.Valid()); a.payload[1] = 0;
    auto q = std::make_unique<Queue<4>>();
    Check(!q->Pop(b));
    Check(q->PushInput(77, 12, 34)); Check(q->Pop(b));
    Check(b.size == 20 && b.kind == Input && b.frame == 77 && b.payload[0] == 12 && b.payload[1] == 34);
    for (int i = 0; i < 4; ++i) { a.frame = 65537+i; Check(q->Push(a)); }
    Check(!q->Push(a));
    for (int i = 0; i < 4; ++i) { Check(q->Pop(b)); Check(b.frame == 65537u+i); }
    Check(!q->Pop(b));
    // スロット周回/同時読書きで欠落・混入を検査。値が先に見えることを確認。
    auto concurrent = std::make_unique<Queue<128>>();
    std::thread producer([&] {
        Record r;
        for (uint32_t i = 1; i <= 200000; ++i) {
            r.Set(Input, i, std::array<uint32_t, 2>{i, ~i});
            while (!concurrent->Push(r)) std::this_thread::yield();
        }
    });
    for (uint32_t i = 1; i <= 200000; ++i) {
        while (!concurrent->Pop(b)) std::this_thread::yield();
        Check(b.frame == i && b.payload[0] == i && b.payload[1] == ~i);
    }
    producer.join();
    StartData start;
    start.p1.epoch = start.p2.epoch = 65536;
    start.p1.revision = start.p2.revision = 1;
    start.p1.confirmed = start.p2.confirmed = 1;
    start.p1.stageConfirmed = 1; start.p1.stage = 1;
    a.Set(Start, 131073, start); Check(a.Valid());
    auto invalid = start; invalid.damageLevel = 5; b.Set(Start, 131073, invalid); Check(!b.Valid());
    b.Set(Selection, 65536, start); Check(b.Valid());
    b.Set(Retry, 65536, uint32_t(2)); Check(b.Valid());
    b.Set(Retry, 65536, uint32_t(3)); Check(!b.Valid());
    b.Set(Selection, 65536, start);
    auto choosing = start;
    choosing.p1.confirmed = choosing.p2.confirmed = choosing.p1.stageConfirmed = 0;
    choosing.p1.stage = 0;
    b.Set(Selecting, 65537, choosing); Check(b.Valid());
    b.Set(Selection, 65537, choosing); Check(!b.Valid());
    b.Set(Start, 131073, choosing); Check(!b.Valid());
    choosing.p1.confirmed = 1;
    b.Set(Selecting, 65538, choosing); Check(b.Valid());
    auto badChoice = choosing; badChoice.p1.moon = 3;
    b.Set(Selecting, 65538, badChoice); Check(!b.Valid());
    auto archive = std::make_unique<Archive<4>>();
    archive->Append(a); Check(archive->Join() == 1);
    for (int i = 0; i < 4; ++i) { b.Set(Input, 131073+i, std::array<uint32_t, 2>{}); archive->Append(b); }
    Check(!archive->Join()); Check(!archive->Get(1)); Check(archive->Get(2));
    archive->Append(a); Check(archive->Join() == 6);
    // 1試合内の次ラウンド・結果では最新試合番号を進めない。次の選択/Startで進める。
    b.Set(Epoch, 196609, start.rng); archive->Append(b); Check(archive->LatestMatch() == 0);
    b.Set(Result, 196700, Score{1, 0, 0, 1}); archive->Append(b); Check(archive->LatestMatch() == 0);
    auto nextMatch = start; nextMatch.score = {1, 0, 0, 1};
    b.Set(Selection, 262144, nextMatch); archive->Append(b); Check(archive->LatestMatch() == 1);
    nextMatch.score = {2, 0, 0, 2};
    b.Set(Start, 327681, nextMatch); archive->Append(b); Check(archive->LatestMatch() == 2);
    // 前の対戦が残っていても、キャラ選択へ戻れば途中参加の入口をそこで置換する。
    choosing.score.revision = 3;
    b.Set(Selecting, 458753, choosing); archive->Append(b);
    Check(archive->Get(archive->Join())->kind == Selecting && archive->LatestMatch() == 3);
    // 実TCP: 試合前接続・heartbeat・後から参加・二重配信・切断。
    auto emblem = std::make_shared<cccaster::emblem::Image>();
    for (unsigned i = 0; i < emblem->pixels.size(); ++i) emblem->pixels[i] = uint8_t(i * 7);
    emblem->id = cccaster::emblem::Hash(emblem->pixels);
    cccaster::emblem::Store::Start(emblem, true);
    cccaster::emblem::Store::Set(1, std::make_shared<const cccaster::emblem::Image>());
    auto host = std::make_unique<Transport>();
    host->StartHost(0); Check(Until([&] { return host->State() == Status::Waiting; }));
    const uint16_t port = host->Port();
    Check(!cccaster::main_app::network_wrapper::FindSpectatorEndpoint({"invalid", "", "127.0.0.1"}, port,
        [] { return false; }).empty());
    auto first = std::make_unique<Transport>(); first->StartViewer("127.0.0.1", port);
    Check(Until([&] { return first->State() == Status::Waiting; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    Check(first->Active()); Check(host->Publish(a));
    for (uint32_t i = 0; i < 200; ++i) { b.Set(Input, 131073+i, std::array<uint32_t, 2>{i, 0}); Check(host->Publish(b)); }
    auto late = std::make_unique<Transport>(); late->StartViewer("127.0.0.1", port);
    Check(Until([&] {
        const auto firstImage = cccaster::emblem::Store::Get(2);
        const auto secondImage = cccaster::emblem::Store::Get(3);
        return firstImage && firstImage->pixels == emblem->pixels && secondImage && !secondImage->id;
    }));
    for (auto *viewer : {first.get(), late.get()}) {
        Check(Until([&] { return viewer->Take(b); })); Check(b.kind == Start);
        for (uint32_t i = 0; i < 200; ++i) {
            Check(Until([&] { return viewer->Take(b); })); Check(b.kind == Input && b.frame == 131073+i && b.payload[0] == i);
        }
    }
    b.Set(Start, 327681, nextMatch); Check(host->Publish(b));
    Check(Until([&] { return first->LatestMatch() == 2 && late->LatestMatch() == 2; }));
    // 実配信が最新試合番号を履歴より前にheartbeatで通知することを確認する。
    asio::io_context io;
    asio::ip::tcp::socket raw(io);
    raw.connect({asio::ip::make_address("127.0.0.1"), port});
    asio::write(raw, asio::buffer(Hello));
    std::array<uint32_t, 4> hello{};
    asio::read(raw, asio::buffer(hello)); Check(hello == Hello);
    asio::read(raw, asio::buffer(&b, 16));
    Check(b.kind == Heartbeat && b.Valid() && b.payload[0] == 2);
    raw.close();
    // 最終ステージをまだ送らず、次のキャラ選択中に接続した観戦者が古いStartへ戻らない。
    b.Set(Selecting, 458753, choosing); Check(host->Publish(b));
    Check(Until([&] { return first->LatestMatch() == 3 && late->LatestMatch() == 3; }));
    auto selectingViewer = std::make_unique<Transport>(); selectingViewer->StartViewer("127.0.0.1", port);
    Check(Until([&] { return selectingViewer->Take(b); }));
    Check(b.kind == Selecting && b.Get<StartData>().p1.confirmed && !b.Get<StartData>().p2.confirmed);
    Check(!selectingViewer->Buffered() && selectingViewer->LatestMatch() == 3);
    auto finalChoice = choosing; finalChoice.p2.confirmed = finalChoice.p1.stageConfirmed = 1; finalChoice.p1.stage = 1;
    b.Set(Selection, 458800, finalChoice); Check(host->Publish(b));
    Check(Until([&] { return selectingViewer->Take(b); })); Check(b.kind == Selection && b.Valid());
    selectingViewer->Stop();
    first->Stop(); Check(Until([&] { return host->Viewers() == 1; }));
    host->Stop(); Check(Until([&] { return late->State() == Status::Disconnected; })); late->Stop();
    // 再生キュー内に新しいStartがまだ無くても最新位置を取得する。旧heartbeatや
    // 過去試合のStartを続けて受けても戻らず、制御通知自体は再生キューへ混ざらない。
    asio::ip::tcp::acceptor feed(io, {asio::ip::tcp::v4(), 0});
    auto behind = std::make_unique<Transport>();
    behind->StartViewer("127.0.0.1", feed.local_endpoint().port());
    asio::ip::tcp::socket sender(io); feed.accept(sender);
    asio::read(sender, asio::buffer(hello)); Check(hello == Hello);
    asio::write(sender, asio::buffer(Hello));
    b.Set(Heartbeat, 0, uint32_t(2)); asio::write(sender, asio::buffer(&b, b.size));
    b.Set(Start, 131073, start); asio::write(sender, asio::buffer(&b, b.size));
    b.Set(Heartbeat, 0, uint32_t(0)); asio::write(sender, asio::buffer(&b, b.size));
    Check(Until([&] { return behind->LatestMatch() == 2 && behind->Buffered() == 1; }));
    Check(behind->Take(b) && b.kind == Start && b.Get<StartData>().score.revision == 0);
    Check(SkipCatchupDrawing(true, b.Get<StartData>().score.revision, behind->LatestMatch()));
    // 次試合へ移ると、確定入力の末尾へ到達する前でも描画を復帰する。
    nextMatch.score.revision = 1; b.Set(Start, 262145, nextMatch);
    asio::write(sender, asio::buffer(&b, b.size));
    Check(Until([&] { return behind->Take(b); }));
    Check(!SkipCatchupDrawing(true, b.Get<StartData>().score.revision, behind->LatestMatch()));
    Check(behind->LatestMatch() == 2 && !behind->Buffered());
    behind->Stop(); sender.close();
    // ホットパスの単独往復。各測定の時計呼出コストも含む。
    std::vector<int64_t> samples; samples.reserve(20000);
    a.Set(Input, 65537, std::array<uint32_t, 2>{0, 0});
    for (unsigned i = 0; i < 20000; ++i) {
        const auto begin = std::chrono::steady_clock::now();
        Check(q->PushInput(65537, 0, 0)); Check(q->Pop(b));
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count());
    }
    std::sort(samples.begin(), samples.end());
    std::cout << "観戦キュー/TCP検査成功: roundtrip ns median=" << samples[10000] << " p99=" << samples[19800] << " max=" << samples.back() << '\n';
}
