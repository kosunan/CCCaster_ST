#include "test_support.hpp"
#include "core_dll/engine/SelectionPreferences.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <atomic>

using namespace cccaster::domain::scene::selection_preferences;
namespace fs = std::filesystem;
std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int main() {
    const auto directory = fs::temp_directory_path() / ("cccaster_display_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory / fs::path(u8"日本語"));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code e; fs::remove_all(path,e); } } cleanup{directory};
    const auto path = directory / fs::path(u8"日本語/display.ini");
    const auto encoded = path.u8string();
    const std::string utf8Path(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    const auto main = directory / "cccaster.ini";
    { std::ofstream file(main); file << "[Settings]\nDelay=6\nPlayerName=KEEP\n"; }
    const auto originalMain = Read(main);
    Store store; store.Load(utf8Path);
    CC_CASE("初回は未設定で、読み込みだけではファイルを生成しない");
    for (unsigned i = 0; i < Names.size(); ++i) CC_CHECK_EQ(store.Get(Key(i)), -1);
    CC_CHECK(!fs::exists(path));

    CC_CASE("8項目を保存し、別インスタンスでOFFと0を含めて復元する");
    CC_CHECK(store.Set(Key::Animation,0));
    CC_CHECK(store.Set(Key::Hud,2));
    CC_CHECK(store.SetResolution(1280,720));
    CC_CHECK(store.Set(Key::Fullscreen,0));
    CC_CHECK(store.Set(Key::CharacterFilter,0));
    CC_CHECK(store.Set(Key::ScreenFilter,1));
    CC_CHECK(store.Set(Key::AspectRatio,6));
    CC_CHECK(store.Set(Key::ViewFps,1));
    Store loaded; loaded.Load(utf8Path);
    for (unsigned i = 0; i < Names.size(); ++i) CC_CHECK_EQ(loaded.Get(Key(i)),store.Get(Key(i)));
    CC_CHECK(Read(path).find("Delay") == std::string::npos);
    CC_CHECK(Read(main) == originalMain);
    CC_CASE("Steam描画側の解像度保存と入力側の表示保存が互いの値を失わない");
    std::atomic<bool> begin{false}, success{true};
    std::thread renderer([&] {
        while (!begin.load()) std::this_thread::yield();
        for (unsigned i=0;i<20;++i) if (!store.SetResolution(1280+int(i)*8,720)) success=false;
    });
    std::thread input([&] {
        while (!begin.load()) std::this_thread::yield();
        for (unsigned i=0;i<20;++i) if (!store.Set(Key::Hud,int(i%3))) success=false;
    });
    begin=true;renderer.join();input.join();
    CC_CHECK(success.load());
    loaded.Load(utf8Path);
    CC_CHECK_EQ(loaded.Get(Key::Width),1432);
    CC_CHECK_EQ(loaded.Get(Key::Height),720);
    CC_CHECK_EQ(loaded.Get(Key::Hud),1);
    // 以下の保存失敗試験が期待する値へ戻す。
    CC_CHECK(loaded.Set(Key::Hud,2));

    CC_CASE("無効な値と片側だけの解像度保存を拒否する");
    const auto saved = Read(path);
    CC_CHECK(!loaded.Set(Key::Hud,3));
    CC_CHECK(!loaded.Set(Key::Count,1));
    CC_CHECK(!loaded.Set(Key::Width,800));
    CC_CHECK(!loaded.SetResolution(0,600));
    CC_CHECK(Read(path) == saved);

    CC_CASE("保存に失敗しても前のファイルと保存値を維持する");
    fs::create_directory(path.string() + ".tmp");
    CC_CHECK(!loaded.Set(Key::Hud,1));
    CC_CHECK_EQ(loaded.Get(Key::Hud),2);
    CC_CHECK(Read(path) == saved);
    fs::remove(path.string() + ".tmp");
    CC_CHECK(loaded.Set(Key::Hud,1));
    CC_CHECK(Read(path.string() + ".bak") == saved);

    CC_CASE("破損・範囲外の保存値を未設定として扱い、有効な項目を保持する");
    { std::ofstream file(path); file << "[Display]\nStageAnimation=0\nHudMode=1junk\nRenderWidth=1280\n"
        "RenderHeight=-2\nFullscreen=2\nCharacterFilter=4\nScreenFilter=1\nAspectRatio=7\nViewFps=99999999999999\n"; }
    loaded.Load(utf8Path);
    CC_CHECK_EQ(loaded.Get(Key::Animation),0);
    CC_CHECK_EQ(loaded.Get(Key::ScreenFilter),1);
    for (auto key : {Key::Hud,Key::Width,Key::Height,Key::Fullscreen,Key::CharacterFilter,Key::AspectRatio,Key::ViewFps})
        CC_CHECK_EQ(loaded.Get(key),-1);
    CC_CHECK(Read(main) == originalMain);
    return cccaster::test::Summarize("selection_preferences");
}
