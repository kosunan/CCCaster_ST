#include "shared_contracts/EmblemFile.hpp"
#include "core_dll/ui/HudLayout.hpp"
#include "core_dll/spectator/Stream.hpp"
#include <cstdio>
#include <cmath>

int main() {
    using namespace cccaster::emblem;
    int failures = 0;
    const auto check = [&](bool yes, const char* label) { if (!yes) { ++failures; std::fprintf(stderr, "FAIL %s\n", label); } };
    Image source;
    for (unsigned i = 0; i < Bytes; ++i) source.pixels[i] = uint8_t(i * 13 + i / 251);
    for (unsigned i = 3; i < Bytes; i += 4) source.pixels[i] = 255;
    source.id = Hash(source.pixels);
    check(Valid(source), "image checksum");
    static_assert(Width == 24 && Height == 12 && ChunkCount == 5);
    const auto tail = MakeChunk(source, ChunkCount - 1);
    check(ChunkSize(tail.index) == 128, "last chunk uses only remaining 128 bytes");
    for (unsigned i = 128; i < ChunkBytes; ++i) check(!tail.pixels[i], "tail padding is zero");
    Receiver receiver;
    auto invalid = MakeChunk(source, 0); invalid.index = ChunkCount;
    check(!receiver.Accept(invalid), "out of bounds chunk rejected");
    auto legacyChunk = MakeChunk(source, 0); legacyChunk.magic = 0x314d4543;
    check(!receiver.Accept(legacyChunk), "old square image format rejected");
    std::shared_ptr<const Image> received;
    for (int i = ChunkCount - 1; i >= 0; --i) {
        auto chunk = MakeChunk(source, unsigned(i));
        auto result = receiver.Accept(chunk);
        if (result) received = result;
        if (i) check(!result, "incomplete image never published");
        check(!receiver.Accept(chunk), "duplicate ignored");
    }
    check(received && received->id == source.id && received->pixels == source.pixels, "out of order exact reconstruction");
    Receiver corrupt;
    for (unsigned i = 0; i < ChunkCount; ++i) {
        auto chunk = MakeChunk(source, i); if (!i) chunk.pixels[0] ^= 1;
        check(!corrupt.Accept(chunk), "corrupt data not published");
    }
    for (unsigned i = 0; i < ChunkCount; ++i) if (auto result = corrupt.Accept(MakeChunk(source, i))) received = result;
    check(corrupt.Id() == source.id, "repair after corruption");
    Exchange a, b;
    a.Start(std::make_shared<const Image>(source)); b.Start(std::make_shared<const Image>());
    bool gotSource = false, gotEmpty = false;
    unsigned sent = 0;
    for (int64_t t = 0; t <= 22000000; t += 10000) {
        Chunk chunk;
        if (a.Next(t, chunk)) { ++sent; if (sent % 7) if (auto image = b.Receive(chunk)) gotSource = image->id == source.id; }
        if (b.Next(t, chunk)) if (auto image = a.Receive(chunk)) gotEmpty = !image->id;
    }
    check(gotSource && gotEmpty && sent < 500, "loss repairs and stops after acknowledgement");
    Exchange legacy; legacy.Start(std::make_shared<const Image>(source)); Chunk chunk;
    check(legacy.Next(0, chunk) && !legacy.Next(21000000, chunk), "legacy peer transfer bounded");
    const auto path = std::filesystem::temp_directory_path() / "cccaster-emblem-unit.bmp";
    Image loaded;
    check(Save(path, source) && Load(path, loaded) && loaded.pixels == source.pixels, "file round trip");
    Image damaged = source; damaged.pixels[0] ^= 1;
    check(!Save(path, damaged) && Load(path, loaded) && loaded.id == source.id, "bad save keeps prior image");
    check(Save(path, Image{}) && !std::filesystem::exists(path), "remove persists absence of BMP");
    check(Save(path, Image{}), "remove is idempotent");
    { std::ofstream out(path, std::ios::binary | std::ios::trunc); out << "bad"; }
    check(!Load(path, loaded), "truncated file rejected");
    std::filesystem::remove(path);
    using cccaster::hud::Layout;
    for (const auto bounds : {cccaster::hud::Rect{0,0,640,480}, {0,0,960,720}, {0,0,1280,960},
                              {0,0,1280,720}, {0,0,1920,1080}, {80,20,800,600}}) {
        auto l = Layout::Fit(bounds);
        check(std::abs(l.X(Layout::Settings.x + Layout::Settings.width / 2) - (bounds.x + bounds.width / 2)) <= 1, "D/R stays centered");
        check(l.X(0) >= bounds.x && l.X(640) <= bounds.x + bounds.width &&
              l.Y(0) >= bounds.y && l.Y(480) <= bounds.y + bounds.height, "viewport bounds");
        check(Layout::Left.x + Layout::Left.width < Layout::Settings.x &&
              Layout::Settings.x + Layout::Settings.width < Layout::Right.x, "player and center regions separated");
        check(Layout::SettingsSize < Layout::NameSize && Layout::GuideSize < Layout::SettingsSize, "font hierarchy");
        check(Layout::SelectionGuide.y >= 430 && Layout::SelectionGuide.y + Layout::SelectionGuide.height <= 480,
              "selection guide keeps clear of character names");
        check(Layout::TrainingGuide.x >= 232 && Layout::TrainingGuide.x + Layout::TrainingGuide.width <= 408 &&
              Layout::TrainingGuide.y + Layout::TrainingGuide.height <= 480, "three-row training guide between circuit gauges");
        for (unsigned player : {0u, 1u}) {
            const auto bar = Layout::Player(player), wins = Layout::Wins(player);
            check(wins.x > bar.x && wins.x + wins.width < bar.x + bar.width &&
                  wins.y >= bar.y && wins.y + wins.height <= bar.y + bar.height, "wins stay in player nameplate");
        }
    }
    cccaster::spectator::Record record;
    record.Set(cccaster::spectator::Emblem, 0, cccaster::spectator::EmblemData{1, MakeChunk(source, 0)});
    check(record.Valid(), "spectator chunk accepted");
    record.payload[0] = 2; check(!record.Valid(), "invalid spectator side rejected");
    return failures ? 1 : 0;
}
