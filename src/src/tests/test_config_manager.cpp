#include "test_support.hpp"
#include "cli_launcher/ConfigManager.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
namespace fs = std::filesystem;
using cccaster::main_app::Config;

struct TemporaryDirectory {
    fs::path path = fs::temp_directory_path() /
        ("cccaster_config_manager_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { fs::create_directory(path); }
    ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
};

std::string Read(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void CheckRoundTrip(Config &source, const fs::path &directory, const std::string &section) {
    const auto plain = directory / "plain.ini";
    const auto checked = directory / "checked.ini";
    source.Save(plain.string());
    CC_CHECK(source.SaveChecked(checked.string()));
    CC_CHECK(Read(plain) == Read(checked));
    for (const auto &path : {plain, checked}) {
        Config loaded;
        loaded.Load(path.string());
        CC_CHECK(loaded.GetString(section, "Name") == "日本語プレイヤー");
        CC_CHECK(loaded.GetString(section, "Empty", "missing").empty());
        CC_CHECK(loaded.GetString(section, "Expression") == "A=B");
        CC_CHECK_EQ(loaded.GetInt(section, "Delay", -1), 4);
    }
}
}

int main() {
    TemporaryDirectory temporary;
    Config source;
    for (const std::string section : {"", "Settings"}) {
        CC_CASE("通常保存と確認付き保存はセクション・日本語・空値・等号を保持する");
        source.Clear();
        source.SetString(section, "Name", "日本語プレイヤー");
        source.SetString(section, "Empty", "");
        source.SetString(section, "Expression", "A=B");
        source.SetInt(section, "Delay", 4);
        CheckRoundTrip(source, temporary.path, section);
    }

    CC_CASE("日本語フォルダーの設定を読書きし、置換保存とバックアップを確認する");
    const auto localized = temporary.path / fs::path(u8"日本語設定");
    fs::create_directory(localized);
    CheckRoundTrip(source, localized, "Settings");
    CC_CHECK(source.SaveChecked((localized / "checked.ini").string()));
    CC_CHECK(fs::exists(localized / "checked.ini.bak"));

    CC_CASE("確認付き保存は上書き前の設定をバックアップに保持する");
    const auto checked = temporary.path / "checked.ini";
    const auto original = Read(checked);
    source.SetInt("Settings", "Delay", 8);
    CC_CHECK(source.SaveChecked(checked.string()));
    CC_CHECK(Read(checked.string() + ".bak") == original);
    Config loaded;
    loaded.Load(checked.string());
    CC_CHECK_EQ(loaded.GetInt("Settings", "Delay"), 8);
    CC_CHECK(!fs::exists(checked.string() + ".tmp"));

    CC_CASE("一時ファイルを作れないときは既存設定とバックアップを保持する");
    const auto saved = Read(checked);
    fs::create_directory(checked.string() + ".tmp");
    source.SetInt("Settings", "Delay", 2);
    CC_CHECK(!source.SaveChecked(checked.string()));
    CC_CHECK(Read(checked) == saved);
    CC_CHECK(Read(checked.string() + ".bak") == original);
    return cccaster::test::Summarize("config_manager");
}
