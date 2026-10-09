#include "core_dll/mbaa_mem/NativeResolution.hpp"
#include "core_dll/mbaa_mem/GameBuildGuard.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/hook/BorderlessDisplay.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/mbaa_mem/SteamV154Signatures.hpp"
#include "core_dll/engine/SelectionPreferences.hpp"
#include <MinHook.h>
#include <atomic>
using cccaster::game_memory::GameRuntime;
#include <cstring>
#include <vector>

namespace cccaster::game_interface::native_resolution {
namespace {
using borderless::Resolution;
void* originalWindow = nullptr;
bool installed = false, attempted = false;
std::atomic<bool> pending{false};
bool saveOnCompletion = false;
Resolution requested{}, previous{}, actual{};
long resetResult = E_PENDING;
constexpr uintptr_t Width = 0x5B4D34, Height = 0x5B4D54;
constexpr uintptr_t KeepWindowMode = 0x7B4568, ResetRequest = 0x5BB5A8;
template<class T> T& At(uintptr_t address) { return *reinterpret_cast<T*>(GameRuntime::Preferred(address,sizeof(T))); }

// Steam 4F3100: ECX=HWND欄、EDX=Windowed。439E60の資源解放/Reset/再生成の後に呼ばれる。
unsigned ForwardWindow(void* function, HWND* window, int windowed) {
    return reinterpret_cast<unsigned (__fastcall*)(HWND*,int)>(function)(window,windowed);
}
__attribute__((force_align_arg_pointer)) unsigned __fastcall WindowHook(HWND* window, int windowed) {
    if (!pending) return ForwardWindow(originalWindow, window, windowed);
    // 資源解放→Reset→資源再生成は元のSteam 439E60で完了済み。
    // この要求だけ既存ボーダーレスの窓処理へ戻し、標準側の排他切替・INI保存を行わない。
    const bool resetOk = SUCCEEDED(resetResult) && actual.width == requested.width && actual.height == requested.height;
    const bool applied = resetOk && borderless::ApplyRenderResolution(actual);
    const auto display = borderless::GetDisplaySettings();
    domain::session::DebugLog(
        "[NativeResolution] COMPLETE applied=%d requested=%dx%d backbuffer=%dx%d fullscreen=%d window=%dx%d hr=0x%08lX",
        int(applied), requested.width, requested.height, actual.width, actual.height, int(display.fullscreen),
        display.windowSize.width, display.windowSize.height, static_cast<unsigned long>(resetResult));
    if (!resetOk) {
        At<uint32_t>(Width) = previous.width;
        At<uint32_t>(Height) = previous.height;
    }
    if (applied && saveOnCompletion) domain::scene::selection_preferences::SaveResolution(actual.width, actual.height);
    saveOnCompletion = false;
    pending.store(false, std::memory_order_release);
    return applied;
}
bool Install() {
    if (attempted) return installed;
    if (!game_build::RuntimeValidated()) return false;
    attempted = true;
    using namespace cccaster::game_memory;
    if (!steam_code::Matches(steam_v154::ResolutionWindow) ||
        !steam_code::Matches(steam_v154::ResolutionRequest) ||
        !steam_code::Matches(steam_v154::ResolutionReset)) return false;
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    auto* site = reinterpret_cast<void*>(GameRuntime::Preferred(0x4F3100));
    if (MH_CreateHook(site,reinterpret_cast<void*>(WindowHook),&originalWindow) != MH_OK) return false;
    if (MH_EnableHook(site) != MH_OK) { MH_RemoveHook(site); originalWindow = nullptr; return false; }
    installed = true;
    return true;
}
Resolution Next(Resolution current, int direction) {
    // Steamの標準RESOLUTIONが使用する16byte単位の候補表。
    const auto begin = At<uintptr_t>(0x7EA514), end = At<uintptr_t>(0x7EA518);
    std::vector<Resolution> modes;
    const auto limit = borderless::ResolutionLimit();
    if (begin && end > begin && end-begin <= 256*16 && (end-begin)%16 == 0 &&
        !IsBadReadPtr(reinterpret_cast<void*>(begin),end-begin)) {
        for (auto item=begin; item<end; item+=16) {
            // 候補表はヒープ上。EXE内の優先VAとして再配置しない。
            const Resolution size{*reinterpret_cast<const int*>(item),
                                  *reinterpret_cast<const int*>(item+4)};
            if (size.width >= 640 && size.height >= 480 && size.width <= limit.width && size.height <= limit.height)
                modes.push_back(size);
        }
    }
    // 起動ダイアログを省略した環境では標準候補表が未生成の場合がある。
    if (modes.empty()) return borderless::NextResolution(current,limit,direction);
    for (size_t i=0; i<modes.size(); ++i)
        if (modes[i].width == current.width && modes[i].height == current.height)
            return modes[(i + (direction>0 ? 1 : modes.size()-1)) % modes.size()];
    return direction>0 ? modes.front() : modes.back();
}
}
ScreenResolution Read() {
    if (!Install() || !At<uintptr_t>(0x7CDB08)) return {};
    const auto width = At<uint32_t>(Width), height = At<uint32_t>(Height);
    if (width < 320 || height < 240 || width > 16384 || height > 16384) return {};
    return {int(width),int(height),true,pending};
}
bool EnableAspectSelection() {
    static bool enabled = false;
    if (enabled) return true;
    if (!game_build::RuntimeValidated() ||
        !game_memory::steam_code::Matches(game_memory::steam_v154::AspectSelection)) return false;
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    // 48AA20..48AA42は画面比からconfig+178へ2/3を毎回上書きするSteam追加処理。
    // 後続のCMPと0..6の標準描画分岐へ接続。XMM0/flagsは分岐先で再定義される。
    auto* site = reinterpret_cast<void*>(GameRuntime::Preferred(0x48AA20));
    auto* resume = reinterpret_cast<void*>(GameRuntime::Preferred(0x48AA43));
    if (MH_CreateHook(site, resume, nullptr) != MH_OK) return false;
    if (MH_EnableHook(site) != MH_OK) { MH_RemoveHook(site); return false; }
    enabled = true;
    domain::session::DebugLog("[NativeResolution] ASPECT manual=1 site=0048AA20 resume=0048AA43");
    return true;
}
bool RequestSize(Resolution next, bool save) {
    const auto state = Read();
    if (!state.available || state.pending || *CC_GAME_MODE_ADDR != CC_GAME_MODE_CHARA_SELECT ||
        At<uint32_t>(ResetRequest) || !borderless::GetDisplaySettings().available) return false;
    if (!next.width || (next.width == state.width && next.height == state.height)) return false;
    previous = {state.width,state.height}; requested = next; actual = {};
    saveOnCompletion = save;
    resetResult = E_PENDING; pending = true;
    // Steam 48A0A0..48A0C5のCHANGE SCREENと同じ要求。Windowedは維持する。
    At<uint32_t>(Width) = next.width;
    At<uint32_t>(Height) = next.height;
    At<uint32_t>(KeepWindowMode) = 1;
    At<uint32_t>(ResetRequest) = 1;
    domain::session::DebugLog("[NativeResolution] REQUEST size=%dx%d fullscreen=%d",
        next.width,next.height,int(borderless::Active()));
    return true;
}
bool Request(int direction) {
    const auto state = Read();
    return direction && state.available && RequestSize(Next({state.width,state.height},direction), true);
}
bool Restore(int width, int height) {
    const auto limit = borderless::ResolutionLimit();
    if (width < 640 || height < 480 || width > limit.width || height > limit.height) {
        // 別モニターでは収まる最大候補へ。保存済みの希望寸法は書き換えない。
        const auto fallback = borderless::NextResolution({}, limit, -1);
        return fallback.width && RequestSize(fallback, false);
    }
    return RequestSize({width,height}, false);
}
void ResetFinished(long result, unsigned width, unsigned height) {
    if (!pending) return;
    resetResult = result;
    actual = {int(width),int(height)};
}
}
