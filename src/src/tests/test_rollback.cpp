#include "core_dll/rollback/PointerSnapshot.hpp"
#include "core_dll/rollback/PredictionHistory.hpp"
#include "core_dll/rollback/GameSnapshotLayout.hpp"
#include "core_dll/rollback/ReplayRoundLocation.hpp"
#include "core_dll/rollback/ReplayCursorBounds.hpp"
#include "core_dll/rollback/IntroSoundClock.hpp"
#include "core_dll/rollback/RollbackStates.hpp"
#include <iostream>
#include <array>
#include <cstdlib>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            std::cerr << "FAILED line " << __LINE__ << "\n";                                                 \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)

struct Model {
    uint32_t x = 0, y = 0, rng = 17;
    bool operator==(const Model &) const = default;
};
static void advance(Model &s, uint32_t a, uint32_t b) {
    s.rng = s.rng * 1664525u + 1013904223u + (a ^ b);
    s.x += a * 3u + b + s.rng;
    s.y ^= b * 7u + a + (s.x >> 3);
}
static int delayedSimulation(uint32_t limit, uint32_t networkLag = 0) {
    using cccaster::sync::PredictionHistory;
    constexpr uint32_t first = 65537, count = 700;
    PredictionHistory history;
    history.Reset(first, limit);
    std::array<Model, 32> saves;
    Model state, baseline;
    uint32_t frontier = first, rollbacks = 0, skipped = 0, deepest = 0;
    auto local = [](uint32_t f) { return ((f % 9) << 16) | ((f * 17) & 1023); };
    auto remote = [](uint32_t f) { return (((f / 3) % 9) << 16) | ((f * 31) & 1023); };
    for (uint32_t f = first; f < first + count; ++f)
        advance(baseline, local(f), remote(f));
    for (uint32_t tick = 0; tick < count + 50; ++tick) {
        auto read = [&](uint32_t f, uint32_t &v) {
            if (f - first + networkLag + (f * 13 % 7) > tick)
                return false;
            v = remote(f);
            return true;
        };
        const auto previouslyConfirmed=history.Confirmed();
        auto mismatch = history.Reconcile(read);
        if (mismatch) {
            CHECK(mismatch > previouslyConfirmed);
            CHECK(frontier - mismatch <= limit);
            deepest = std::max(deepest, frontier - mismatch);
            ++rollbacks;
            state = saves[mismatch % 32];
            for (uint32_t f = mismatch; f < frontier; ++f) {
                uint32_t a, b;
                CHECK(history.ResolveReplay(f, read, a, b));
                if (history.NeedsReplaySnapshot(f)) saves[f % 32] = state;
                else { saves[f % 32] = {0xdeadbeef,0xbadf00d,0}; ++skipped; }
                advance(state, a, b);
            }
        }
        if (frontier == first + count) {
            if (history.Confirmed() == frontier - 1)
                break;
            continue;
        }
        uint32_t value;
        if (!read(frontier, value)) {
            if (!history.CanPredict())
                continue;
            value = history.Prediction();
        }
        saves[frontier % 32] = state;
        CHECK(history.Record(frontier, local(frontier), value));
        advance(state, local(frontier), value);
        ++frontier;
    }
    CHECK(frontier == first + count);
    CHECK(history.Confirmed() == frontier - 1);
    CHECK(state == baseline);
    CHECK(limit == 0 || rollbacks > 50);
    CHECK(limit == 0 || skipped > 50);
    CHECK(networkLag < limit || deepest == limit);
    return 0;
}

// 自入力も未採取の1枠を保持予測する。相手入力の到着が遅れていても、
// Present前で自入力を訂正し、全確定後は同じ入力列を逐次実行した状態と一致する。
static int bothInputsSimulation(uint32_t networkLag) {
    using cccaster::sync::PredictionHistory;
    constexpr uint32_t first=65537, count=300;
    PredictionHistory history;history.Reset(first,20);
    std::array<Model,32> saves{};
    Model state,baseline;
    uint32_t frontier=first,localCorrections=0;
    auto local=[](uint32_t f) { return (f*17)%31; };
    auto remote=[](uint32_t f) { return (f*23)%29; };
    for (uint32_t f=first;f<first+count;++f) advance(baseline,local(f+1),remote(f+1));
    for (uint32_t tick=0;tick<count+networkLag+80;++tick) {
        auto readLocal=[&](uint32_t f,uint32_t &v) { v=local(f+1);return f-first+1<=tick; };
        auto readRemote=[&](uint32_t f,uint32_t &v) { v=remote(f+1);return f-first+1+networkLag<=tick; };
        for (uint32_t f=history.Confirmed()+1;f<history.Next();++f) {
            uint32_t v; if (readLocal(f,v) && history.Get(f)->local!=v) ++localCorrections;
        }
        const auto mismatch=history.Reconcile(readLocal,readRemote);
        if (mismatch) {
            state=saves[mismatch%32];
            for (uint32_t f=mismatch;f<frontier;++f) {
                uint32_t a,b;CHECK(history.ResolveReplay(f,readLocal,readRemote,a,b));
                if (history.NeedsReplaySnapshot(f)) saves[f%32]=state;
                advance(state,a,b);
            }
        }
        if (frontier==first+count) {
            if (history.Confirmed()==frontier-1) break;
            continue;
        }
        if (!history.CanPredict()) continue;
        uint32_t a,b;
        if (!readLocal(frontier,a)) a=history.LocalPrediction();
        if (!readRemote(frontier,b)) b=history.Prediction();
        saves[frontier%32]=state;CHECK(history.Record(frontier,a,b));
        advance(state,a,b);++frontier;
    }
    CHECK(frontier==first+count);CHECK(history.Confirmed()==frontier-1);
    CHECK(state==baseline);CHECK(localCorrections>0);
    return 0;
}

struct SnapshotMemory : cccaster::game_interface::IGameMemory {
    uint32_t value = 0;
    bool IsAvailable() const override {
        return true;
    }
    uint32_t GameMode() const override {
        return 1;
    }
    uint8_t IntroState() const override {
        return 0;
    }
    uint32_t WorldTimer() const override {
        return value;
    }
    uint32_t RealTimer() const override {
        return value;
    }
    uint32_t MenuStateCounter() const override {
        return 0;
    }
    void WriteInput(cccaster::game_interface::GameInput, cccaster::game_interface::GameInput) override {}
    bool SaveSnapshot(std::span<char> b) override {
        if (b.size() != 4)
            return false;
        std::memcpy(b.data(), &value, 4);
        return true;
    }
    bool LoadSnapshot(std::span<char> b) override {
        if (b.size() != 4)
            return false;
        std::memcpy(&value, b.data(), 4);
        return true;
    }
};
static int snapshotChains() {
    using namespace cccaster::sync;
    // ゲームのポインターは32bit。64bitホストでは実アドレスを切り詰めない。
    if (sizeof(uintptr_t) != 4) return 0;
    std::array<char, 65> memory{}, replacement{};
    const auto address = [&](size_t offset) { return reinterpret_cast<uintptr_t>(memory.data() + offset); };
    const auto write = [&](size_t offset, uint32_t value) { std::memcpy(memory.data() + offset, &value, 4); };
    const auto read = [&](size_t offset) { uint32_t value; std::memcpy(&value, memory.data() + offset, 4); return value; };
    // 非整列、子から孫へ、途中ノードを後から参照する枝、非ゼロoffsetの通常経路。
    const SnapshotNode nodes[] = {
        {-1, address(1), 0, 4}, {0, 0, 0, 4}, {1, 0, 0, 4},
        {0, 0, 8, 4}, {1, 0, 8, 4}, {-1, address(49), 0, 8},
        {5, 4, 4, 4}, {6, 0, 0, 4}, {7, 0, 0, 4},
    };
    PointerSnapshot snapshot;
    CHECK(snapshot.Configure(nodes));
    CHECK(snapshot.Size() == 40);
    std::vector<char> bytes(snapshot.Size());
    for (unsigned mask = 0; mask < 32; ++mask) {
        memory.fill(char(0x5a));
        write(1, mask & 1 ? uint32_t(address(9)) : 0);
        write(9, mask & 2 ? uint32_t(address(25)) : 0);
        write(25, 0x12345678);
        write(17, 0x23456789);
        write(33, 0x3456789a);
        write(49, 0x456789ab);
        write(53, mask & 4 ? uint32_t(address(37)) : 0); // offset4でaddress(41)へ
        write(41, mask & 8 ? uint32_t(address(45)) : 0);
        write(45, mask & 16 ? uint32_t(address(57)) : 0);
        write(57, 0x56789abc);
        const auto before = memory;
        CHECK(snapshot.Save(bytes));
        const uint32_t expected[] = {
            read(1), mask & 1 ? read(9) : 0, (mask & 3) == 3 ? read(25) : 0,
            mask & 1 ? read(17) : 0, (mask & 3) == 3 ? read(33) : 0,
            read(49), read(53), mask & 4 ? read(41) : 0,
            (mask & 12) == 12 ? read(45) : 0,
            (mask & 28) == 28 ? read(57) : 0,
        };
        CHECK(std::memcmp(bytes.data(), expected, sizeof(expected)) == 0);
        // 現在のポインターを別領域へ変えても保存時の親を先に復元する。
        memory.fill(char(0x6b));
        replacement.fill(char(0x7c));
        write(1, uint32_t(reinterpret_cast<uintptr_t>(replacement.data() + 1)));
        write(53, uint32_t(reinterpret_cast<uintptr_t>(replacement.data() + 9)));
        const auto untouched = replacement;
        auto restored = memory;
        const auto expectRestore = [&](size_t offset, size_t size = 4) {
            std::memcpy(restored.data() + offset, before.data() + offset, size);
        };
        expectRestore(1);
        if (mask & 1) { expectRestore(9); expectRestore(17); }
        if ((mask & 3) == 3) { expectRestore(25); expectRestore(33); }
        expectRestore(49, 8);
        if (mask & 4) expectRestore(41);
        if ((mask & 12) == 12) expectRestore(45);
        if ((mask & 28) == 28) expectRestore(57);
        CHECK(snapshot.Load(bytes));
        CHECK(memory == restored && replacement == untouched);
        CHECK(!snapshot.Save(std::span<char>(bytes).first(bytes.size() - 1)));
        CHECK(!snapshot.Load(std::span<char>(bytes).first(bytes.size() - 1)));
    }
    // 同じ実アドレスへ循環する鎖も、表の3ノード分だけ処理する。
    write(1, uint32_t(address(1)));
    CHECK(snapshot.Configure(std::span(nodes).first(3)));
    bytes.resize(snapshot.Size());
    CHECK(snapshot.Save(bytes));
    write(1, 0);
    CHECK(snapshot.Load(bytes));
    CHECK(read(1) == address(1));
    // null親の子に非ゼロの保存値があっても、その領域には書き込まない。
    std::memset(bytes.data(), 0xff, bytes.size());
    std::memset(bytes.data(), 0, 4);
    CHECK(snapshot.Load(bytes));
    CHECK(read(1) == 0);
    const SnapshotNode invalid[] = {{0, 0, 0, 4}};
    CHECK(!snapshot.Configure(invalid));
    CHECK(!snapshot.Save(bytes) && !snapshot.Load(bytes));
    CHECK(snapshot.Configure(std::span(nodes).first(1)));
    bytes.resize(snapshot.Size());
    CHECK(snapshot.Save(bytes) && snapshot.Load(bytes));
    return 0;
}
int main() {
    CHECK(snapshotChains() == 0);
    CHECK(bothInputsSimulation(0) == 0);
    CHECK(bothInputsSimulation(5) == 0);
    CHECK(bothInputsSimulation(22) == 0);
    {
        cccaster::sync::PredictionHistory pending;pending.Reset(10,7);
        CHECK(pending.Record(10,0,0));CHECK(pending.Record(11,0,0));
        auto own=[](uint32_t f,uint32_t &v) { v=f==11 ? 4 : 0;return true; };
        auto absent=[](uint32_t,uint32_t &) { return false; };
        CHECK(pending.Reconcile(own,absent)==11); // 古い相手入力の欠落で新しい自入力を止めない。
        CHECK(pending.Confirmed()==9);CHECK(pending.Get(11)->local==4);
        auto peer=[](uint32_t,uint32_t &v) { v=0;return true; };
        CHECK(pending.Reconcile(own,peer)==0);CHECK(pending.Confirmed()==11);
    }
    CHECK(delayedSimulation(0) == 0);
    CHECK(delayedSimulation(4) == 0);
    CHECK(delayedSimulation(8) == 0);
    CHECK(delayedSimulation(7, 20) == 0);
    CHECK(delayedSimulation(20, 20) == 0);
    using namespace cccaster::sync;
    {
        PredictionHistory h;
        h.Reset(1, 4);
        for (uint32_t f = 1; f <= 4; ++f) CHECK(h.Record(f, 0, 0));
        CHECK(!h.CanPredict());
        auto latestOnly = [](uint32_t f, uint32_t &v) { v = 9; return f == 5; };
        CHECK(!h.ReadyToResume(latestOnly, true));
        auto oldestOnly = [](uint32_t f, uint32_t &v) { v = 9; return f == 1; };
        CHECK(h.ReadyToResume(oldestOnly, true));
        CHECK(!h.ReadyToResume(oldestOnly, false));
        CHECK(h.Confirmed() == 0); // 待機判定で不一致を消費しない。
        CHECK(h.Reconcile(oldestOnly) == 1);
        CHECK(h.CanPredict());
        CHECK(h.Record(5, 0, 9));
        CHECK(!h.CanPredict()); // 4F上限は緩めない。
        h.Reset(5, 0);
        CHECK(!h.ReadyToResume(oldestOnly, true));
        CHECK(h.ReadyToResume(latestOnly, true));
    }
    SnapshotMemory memory;
    RollbackStates ring;
    ring.Reset(4);
    const int oldRound = std::fegetround();
    std::fesetround(FE_DOWNWARD);
    memory.value = 10;
    CHECK(ring.Save(1, memory));
    std::fesetround(FE_UPWARD);
    memory.value = 20;
    CHECK(ring.Load(1, memory));
    CHECK(memory.value == 10 && std::fegetround() == FE_DOWNWARD);
    constexpr uint32_t capacity = cccaster::public_api::NetplaySettings::RollbackHistoryFrames;
    // 最大7Fを戻す間、最古の保存状態を上書きしない。
    for (uint32_t f = 2; f <= 8; ++f) {
        memory.value = f;
        CHECK(ring.Save(f, memory));
    }
    CHECK(ring.Load(1, memory) && memory.value == 10);
    memory.value = 30;
    CHECK(ring.Save(capacity + 1, memory));
    CHECK(!ring.Load(1, memory));
    CHECK(ring.Load(capacity + 1, memory) && memory.value == 30);
    for (uint32_t f = capacity + 2; f <= capacity * 2 + 1; ++f) {
        memory.value = f;
        CHECK(ring.Save(f, memory, f + 100, f + 200));
    }
    for (uint32_t f = capacity + 2; f <= capacity * 2 + 1; ++f) {
        CHECK(ring.Export(f, [&](const auto &bytes, const auto &, uint32_t local, uint32_t remote) {
            uint32_t value = 0; std::memcpy(&value, bytes.data(), 4);
            return value == f && local == f + 100 && remote == f + 200;
        }));
    }
    CHECK(!ring.Export(capacity + 1, [](const auto &, const auto &, auto, auto) { return true; }));
    ring.Reset(4);
    CHECK(!ring.Export(capacity * 2 + 1, [](const auto &, const auto &, auto, auto) { return true; }));
    CHECK(!ring.Load(capacity + 2, memory));
    std::fesetround(oldRound);
    PointerSnapshot p;
    CHECK(p.Configure(GameSnapshotLayout));
    CHECK(p.Size() == 1241295); // 進行値20B、演出用乱数228Bを含む。
    // 未作成の次ラウンドを前ラウンド末尾と混同せず、再確保後の現アドレスで解決する。
    CHECK(LocateReplayRound(0, 0, 0).valid && !LocateReplayRound(0, 0, 0).address);
    CHECK(LocateReplayRound(0x1000, 0x1140, 1).valid && !LocateReplayRound(0x1000, 0x1140, 1).address);
    CHECK(LocateReplayRound(0x8000, 0x8280, 1).address == 0x8140);
    CHECK(!LocateReplayRound(0x8000, 0x8280, 3).valid);
    CHECK(!LocateReplayRound(0x8000, 0x7fff, 0).valid);
    CHECK(!LocateReplayRound(0x8000, 0x8281, 0).valid);
    // 実報告: ダミーが一巡するとindex=23から3へ戻る。再生位置なら前方復元を許す。
    CHECK(CanRestoreReplayCursor(23, 32 * 8, true, 3, 32 * 8, true));
    CHECK(CanRestoreReplayCursor(23, 32 * 8, true, 23, 32 * 8, true));
    CHECK(!CanRestoreReplayCursor(23, 32 * 8, true, 3, 32 * 8, false));
    CHECK(CanRestoreReplayCursor(23, 24 * 8, true, 31, 32 * 8, false));
    CHECK(!CanRestoreReplayCursor(32, 32 * 8, true, 3, 32 * 8, true));
    CHECK(!CanRestoreReplayCursor(-1, 32 * 8, true, 3, 32 * 8, true));
    CHECK(!CanRestoreReplayCursor(23, 32 * 8, true, 3, 31 * 8, true));
    CHECK(!CanRestoreReplayCursor(23, 32 * 8, true, 3, 33 * 8, true));
    CHECK(CanRestoreReplayCursor(0, 0, false, 0, 0, true));
    CHECK(IntroSoundClock::Frames(192000, 4, 48000) == 60);
    CHECK(IntroSoundClock::ControlsScript(true, 1, false, false));
    CHECK(IntroSoundClock::ControlsScript(true, 2, false, false));
    CHECK(IntroSoundClock::ControlsScript(true, 0, true, true));
    CHECK(!IntroSoundClock::ControlsScript(true, 0, true, false));
    CHECK(!IntroSoundClock::ControlsScript(true, 0, false, false));
    CHECK(!IntroSoundClock::ControlsScript(false, 0, true, true));
    CHECK(IntroSoundClock::Frames(192004, 4, 48000) == 61);
    IntroSoundClock::duration[400] = 60;
    IntroSoundClock::Start(400, 131073);
    const auto savedSound = IntroSoundClock::until;
    CHECK(IntroSoundClock::Playing(400, 131132));
    CHECK(!IntroSoundClock::Playing(400, 131133));
    IntroSoundClock::Start(400, 131100);
    IntroSoundClock::until = savedSound;
    CHECK(!IntroSoundClock::Playing(400, 131133));
    std::array<uint32_t, 4> a{1, 2, 3, 4};
    SnapshotNode layout[] = {{-1, reinterpret_cast<uintptr_t>(a.data()), 0, 4},
                             {-1, reinterpret_cast<uintptr_t>(&a[3]), 0, 4}};
    CHECK(p.Configure(layout));
    std::vector<char> bytes(p.Size());
    CHECK(p.Save(bytes));
    a = {10, 20, 30, 40};
    CHECK(p.Load(bytes));
    CHECK(a[0] == 1 && a[1] == 20 && a[2] == 30 && a[3] == 4);
    CHECK(!p.Configure({}));
    CHECK(!p.Load(bytes));
    if (sizeof(uintptr_t) == 4) {
        uint32_t child = 77, other = 99;
        uint32_t root = uint32_t(reinterpret_cast<uintptr_t>(&child));
        SnapshotNode tree[] = {{-1, reinterpret_cast<uintptr_t>(&root), 0, 4}, {0, 0, 0, 4}};
        CHECK(p.Configure(tree));
        bytes.resize(p.Size());
        CHECK(p.Save(bytes));
        root = uint32_t(reinterpret_cast<uintptr_t>(&other));
        child = 0;
        CHECK(p.Load(bytes));
        CHECK(child == 77 && other == 99);
        root = 0;
        CHECK(p.Save(bytes));
        root = uint32_t(reinterpret_cast<uintptr_t>(&other));
        CHECK(p.Load(bytes));
        CHECK(root == 0 && other == 99);
    }
    PredictionHistory h;
    h.Reset(65537, 4);
    for (uint32_t f = 65537; f < 65541; ++f) {
        CHECK(h.CanPredict());
        CHECK(h.Record(f, 0x60001, 0x40002));
    }
    CHECK(!h.CanPredict());
    CHECK(!h.Record(65540, 0, 0));
    auto read = [](uint32_t f, uint32_t &value) {
        if (f > 65538)
            return false;
        value = f == 65537 ? 0x40002 : 0x60008;
        return true;
    };
    CHECK(h.Reconcile(read) == 65538);
    CHECK(h.Confirmed() == 65538);
    CHECK(h.CanPredict());
    uint32_t local, remote;
    CHECK(h.ResolveReplay(65539, read, local, remote));
    CHECK(local == 0x60001 && remote == 0x60008);
    CHECK(h.ResolveReplay(65540, read, local, remote));
    CHECK(remote == 0x60008);
    h.Reset(131073, 0);
    CHECK(!h.CanPredict());
    CHECK(!h.Get(65537));
    std::cout << "rollback snapshot and prediction tests passed\n";
}
