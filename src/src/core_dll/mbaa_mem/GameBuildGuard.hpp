#pragma once
#include "shared_contracts/GameBuild.hpp"
#include "shared_contracts/GameLoadedCode.hpp"
#include "core_dll/mbaa_mem/GameRuntime.hpp"
#include <windows.h>

namespace cccaster::game_build {
// DllMainの最初に実施。別の注入器を使っても旧版の固定アドレスへ書かない。
// 旧版は再配置不可。Steam版はファイルのコードと再配置後のコードを照合する。
inline bool ValidateSteamCode(std::span<const uint8_t> loadedText, uint32_t base) {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    const bool sized = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 64000000;
    HANDLE mapping = sized ? CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
    const auto* bytes = mapping ? static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) : nullptr;
    bool valid = false;
    if (bytes) {
        try {
            valid = IdentifyFile({bytes, size_t(size.QuadPart)}) == Edition::Steam20170105 &&
                ValidateLoadedCode({bytes, size_t(size.QuadPart)}, loadedText, base);
        } catch (...) { valid = false; }
        UnmapViewOfFile(bytes);
    }
    if (mapping) CloseHandle(mapping);
    CloseHandle(file);
    return valid;
}
inline bool ValidateLoadedRuntime() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!base) return false;
    PeIdentity p;
    if (!ReadHeaders({reinterpret_cast<const uint8_t *>(base), 4096}, p)) return false;
    const auto edition = IdentifyHeaders(p);
    if (!SupportsRuntime(edition)) return false;
    const auto *text = reinterpret_cast<const uint8_t *>(base + p.textRva);
    uintptr_t cursor = reinterpret_cast<uintptr_t>(text);
    const uintptr_t end = cursor + p.textSize;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void *>(cursor), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
            info.AllocationBase != reinterpret_cast<void *>(base)) return false;
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return ValidateSteamCode({text, p.textSize}, uint32_t(base)) &&
        game_memory::GameRuntime::Initialize(edition, {base, p.imageSize});
}
}
