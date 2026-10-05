#pragma once
#include <filesystem>
#include <stdexcept>

namespace cccaster {
// 新設定を優先し、旧設定は消さずに初回だけ複製する。失敗を既定値へ隠さない。
inline std::filesystem::path ConfigPath(const std::filesystem::path& directory) {
    const auto current = directory / "cccaster_steam.ini";
    const auto legacy = directory / "cccaster_steam_legacy.ini";
    if (!std::filesystem::exists(current) && std::filesystem::exists(legacy)) {
        std::error_code error;
        std::filesystem::copy_file(legacy, current, std::filesystem::copy_options::none, error);
        if (error && !std::filesystem::exists(current))
            throw std::runtime_error("Cannot migrate cccaster_steam_legacy.ini: " + error.message());
    }
    return current;
}
}
