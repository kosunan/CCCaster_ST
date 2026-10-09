#include "core_dll/hook/HookBatch.hpp"
#include "core_dll/mbaa_mem/TrainingCharacterMenu.hpp"
#include "core_dll/mbaa_mem/TrainingPaletteMenu.hpp"
#include "core_dll/mbaa_mem/TrainingHitboxMenu.hpp"
#include "core_dll/mbaa_mem/RealGameMemory.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/mbaa_mem/MbaaInputDefs.hpp"
#include "core_dll/mbaa_mem/GameBuildGuard.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/common/Platform.hpp"
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "SteamV15Signatures.hpp"
#include "SteamTrainingV158Signatures.hpp"
using cccaster::game_memory::GameRuntime;

// SteamのABI・STL配置は steam_training_v15.asm に照合記録を残す。
extern "C" { uintptr_t cc_training_presentation_loader=0; }
__attribute__((naked)) void cc_training_load_presentation(unsigned, unsigned, unsigned, unsigned) {
    // Steam 47D9D0: ECX=slot、EDX=side、stack=character/予約2DWORD/CUT番号。callerが16byte回収。
    __asm__ __volatile__("movl 4(%esp),%ecx; movl 12(%esp),%edx; pushl 16(%esp);"
                         "pushl $0; pushl $0; pushl 20(%esp);"
                         "call *_cc_training_presentation_loader; addl $16,%esp; ret");
}
void cc_training_append(void* vector, void* item) {
    reinterpret_cast<void (__thiscall*)(void*,void*)>(GameRuntime::Preferred(0x41f960))(vector,&item);
}
void cc_training_describe(void* description, void* loading) {
    reinterpret_cast<void (__thiscall*)(void*,void*)>(GameRuntime::Preferred(0x4a0150))(loading,description);
}
uint32_t cc_training_file_size(void* file) {
    return reinterpret_cast<uint32_t (__thiscall*)(void*)>(GameRuntime::Preferred(0x46b2f0))(file);
}
bool cc_training_file_exists(const char* path) {
    // 51F410の索引検索部分。ファイルの生成・読込みは行わない。
    const auto* list=reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(0x7d5ea4,12));
    if(!list[0] || list[1]<list[0] || (list[1]-list[0])/4>65536) return false;
    const auto* begin=reinterpret_cast<uint32_t* const*>(list[0]);
    const auto* end=reinterpret_cast<uint32_t* const*>(list[1]);
    for(auto current=begin;current!=end;++current) {
        if(!*current) continue;
        auto* archive=reinterpret_cast<uint32_t*>((*current)[0]);
        if(archive && archive[1] && reinterpret_cast<void* (__thiscall*)(void*,const char*)>(
            GameRuntime::Preferred(0x51fbd0))(archive,path)) return true;
    }
    return false;
}
// 519DB0: ECX=path、EDX=file**、残り2引数はcallerが回収する。
__attribute__((naked)) int cc_training_read_image(const char*,void*,uintptr_t) {
    __asm__ __volatile__("movl 4(%esp),%ecx\n\tmovl 8(%esp),%edx\n\tmovl 12(%esp),%eax\n\tpushl $0\n\tpushl $0\n\t"
                         "call *%eax\n\taddl $8,%esp\n\tret\n\t");
}

namespace cccaster::game_interface { bool ConfigureMenuObserver(); }
namespace cccaster::training_character {
namespace {
using domain::session::DebugLog;
Selection selection;
bool installed = false, pending = false, restartDispatched = false, changed = false, suppressUntilRelease = false;
uint32_t* mainMenu = nullptr;
uint32_t* menuSet = nullptr;
uint32_t previousButtons = 0;
uint16_t previousDirection = 0;
int64_t repeatAt = 0;
const char* error = "";
using Constructor = uint32_t* (__thiscall*)(uint32_t*);
using Reset = void (__thiscall*)(void*);
Constructor originalConstructor = nullptr;
Reset originalReset = nullptr;

uint32_t* Descriptor(uint32_t character) {
    auto* table = *reinterpret_cast<uint32_t**>(GameRuntime::Preferred(0x5c5144));
    if (!table || character >= table[3]) return nullptr;
    return reinterpret_cast<uint32_t*>(table[0] == 0 ? table[1] + table[2] * character
                                                     : reinterpret_cast<uint32_t*>(table[1])[character]);
}
const char* NativeString(uint32_t* base) {
    return base[5] < 16 ? reinterpret_cast<const char*>(base)
                        : reinterpret_cast<const char*>(base[0]);
}
void AddInformation(uint32_t* menu, const char* key, const char* text) {
    auto* information = reinterpret_cast<uint8_t*>(menu[0xDC/4]);
    if (!information) return;
    // Information\\Training.iniの読込みと同じ文字列形式・アロケータを使用する。
    // 元のINIは変更せず、メニューが所有する説明一覧へ登録する。
    // Steamは24byte文字列2個、説明vectorは情報object+0xB0。
    // 51E96C..51EA48の生成／登録／破棄と同じ呼出規約を使う。
    uint32_t entry[12]{};
    entry[5] = entry[11] = 15;
    const auto assign = reinterpret_cast<void (__thiscall*)(void*, const char*, size_t)>(GameRuntime::Preferred(0x403f50));
    assign(entry, key, std::strlen(key));
    assign(entry + 6, text, std::strlen(text));
    reinterpret_cast<void (__thiscall*)(void*,void*)>(GameRuntime::Preferred(0x51ee50))(information + 0xB0, entry);
    for (auto* value : {entry, entry + 6})
        if (value[5] >= 16) reinterpret_cast<void (__cdecl*)(void*)>(GameRuntime::Preferred(0x52c38e))(reinterpret_cast<void*>(value[0]));
}
bool Exists(const char* path) {
    const auto attributes = GetFileAttributesA(path);
    return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) ||
           cc_training_file_exists(path);
}
bool Available(Choice choice) {
    auto* info = Descriptor(choice.character);
    if (!info || choice.moon != MoonValue(choice.character,MoonSlot(choice.moon))) return false;
    const auto* first = reinterpret_cast<const char*>(info) + 0x34;
    const auto* partner = first + 0x20;
    char path[128]{};
    std::snprintf(path, sizeof(path), ".\\data\\%.31s_%u.txt", first, choice.moon);
    if (!Exists(path)) return false;
    if (*partner && *partner != '0') {
        std::snprintf(path, sizeof(path), ".\\data\\%.31s_%u.txt", partner, choice.moon);
        if (!Exists(path)) return false;
    }
    return true;
}
uint32_t FindItem(uint32_t* set, const char* key) {
    auto** begin = reinterpret_cast<uint32_t**>(set[0x40/4]);
    auto** end = reinterpret_cast<uint32_t**>(set[0x44/4]);
    if (!begin || end < begin || end - begin > 64) return UINT32_MAX;
    for (auto** item = begin; item != end; ++item)
        if (*item && std::strcmp(NativeString(*item + 0x38/4), key) == 0) return uint32_t(item-begin);
    return UINT32_MAX;
}
__attribute__((force_align_arg_pointer)) uint32_t* __fastcall Construct(uint32_t* self, void*) {
    auto* result = originalConstructor(self);
    mainMenu = result;
    menuSet = nullptr;
    selection.open = false;
    if (!result || !result[3] || result[4] <= result[3]) return result;
    auto* set = *reinterpret_cast<uint32_t**>(result[3]);
    if (!set || set[0] != GameRuntime::Preferred(0x588ed4)) return result;
    auto* item = static_cast<uint32_t*>(reinterpret_cast<void* (__cdecl*)(size_t)>(GameRuntime::Preferred(0x52cd13))(0x50));
    if (!item) return result;
    reinterpret_cast<void* (__thiscall*)(void*, const char*, const char*, int)>(GameRuntime::Preferred(0x435100))(
        item, "CHARACTER", "CC_CHARACTER", 0);
    item[0] = GameRuntime::Preferred(0x583600); item[1] = item[3] = 1;
    cc_training_append(set + 0x40/4, item);
    AddInformation(result, "CC_CHARACTER", "Change the character and Moon style for P1 or P2.");
    auto** begin = reinterpret_cast<uint32_t**>(set[0x40/4]);
    auto** end = reinterpret_cast<uint32_t**>(set[0x44/4]);
    std::rotate(begin, end - 1, end);
    const auto addMenu = [&](const char* label, const char* key, const char* explanation, unsigned position) {
        auto* extra = static_cast<uint32_t*>(reinterpret_cast<void* (__cdecl*)(size_t)>(GameRuntime::Preferred(0x52cd13))(0x50));
        if (!extra) return;
        reinterpret_cast<void* (__thiscall*)(void*, const char*, const char*, int)>(GameRuntime::Preferred(0x435100))(
            extra, label, key, 0);
        extra[0] = GameRuntime::Preferred(0x583600); extra[1] = extra[3] = 1;
        cc_training_append(set + 0x40/4, extra);
        AddInformation(result, key, explanation);
        begin = reinterpret_cast<uint32_t**>(set[0x40/4]);
        end = reinterpret_cast<uint32_t**>(set[0x44/4]);
        std::rotate(begin + position, end - 1, end);
    };
    addMenu("COLOR PALETTE", "CC_PALETTE", "Edit character colors and save or load palettes.", 1);
    addMenu("HITBOX", "CC_HITBOX", "Show or hide hitboxes and other collision boxes.", 2);
    set[0x38/4] = set[0x3c/4] = 0;
    menuSet = set;
    error = "";
    // 起動済みのアーカイブ索引で確認。描画中や毎フレームの探索は行わない。
    for (unsigned i = 0; i < Characters.size(); ++i) {
        selection.available[i] = 0;
        for (unsigned moon = 0; moon < MoonCount; ++moon)
            if (Available({Characters[i],MoonValue(Characters[i],moon)})) selection.available[i] |= 1u << moon;
    }
    static bool loggedCatalogue = false;
    if (!loggedCatalogue) {
        for (unsigned i = 0; i < Characters.size(); ++i)
            DebugLog("[TrainingCharacter] OPTION char=%u styles=%u",Characters[i],selection.available[i]);
        loggedCatalogue = true;
    }
    DebugLog("[TrainingCharacter] MENU added count=%u", unsigned(end-begin));
    return result;
}
__attribute__((force_align_arg_pointer)) void __fastcall RoundReset(void* battle, void*) {
    if (pending && *CC_GAME_MODE_ADDR == CC_GAME_MODE_IN_GAME &&
        *reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x5ca794)) == 0x1010) {
        pending = false;
        const auto side = selection.player;
        const auto choice = selection.choice;
        auto* final = reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x7b4370)) + side * 11;
        auto* cursor = side ? CC_P2_SELECTOR_MODE_ADDR : CC_P1_SELECTOR_MODE_ADDR;
        const auto started = platform::RealMonotonicUs();
        DebugLog("[TrainingCharacter] LOAD begin side=%u char=%u moon=%u old=%u/%u stage=%u",
                 side, choice.character, choice.moon, final[2], final[5], *CC_STAGE_SELECTOR_ADDR);
        final[2] = choice.character; final[5] = choice.moon;
        // 特殊キャラの選択時フラグを次のキャラへ持ち越さない。
        final[9] = final[10] = 0;
        // 通常キャラ選択へ戻るカーソルには、隠しID・スタイルを渡さない。
        const auto cursorCharacter = CursorCharacter(choice.character);
        cursor[3] = core::sync::SelectionState::CharacterCell(cursorCharacter);
        cursor[4] = cursorCharacter; cursor[5] = choice.moon < 3 ? choice.moon : 0;
        uint32_t description[0xB8/4]{}, loading[3]{};
        cc_training_describe(description, loading);
        reinterpret_cast<void (__cdecl*)()>(GameRuntime::Preferred(0x472ff0))();
        // Steam通常ロード4A00D0のキャラ資産部分。背景5077D0は呼ばない。
        reinterpret_cast<int (__fastcall*)(void*, void*)>(GameRuntime::Preferred(0x49f9e0))(description, nullptr);
        // ボス差分にない表示画像だけを元キャラから補う。戦闘定義や背景には触れない。
        for (unsigned slot = 0; slot < 4; ++slot) {
            const auto* actor = reinterpret_cast<const uint8_t*>(GameRuntime::Preferred(0x5bc370) + slot * 0xAFC);
            if (!actor[0]) continue;
            const auto character = uint32_t(actor[5]);
            const auto presentation = PresentationCharacter(character);
            if (presentation == character) continue;
            cc_training_load_presentation(slot, presentation, slot % 2, presentation);
            const auto* images = reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(0x5cb530) + slot * 0x20);
            unsigned cutMask = 0;
            for (unsigned i = 0; i < 5; ++i) if (images[2+i]) cutMask |= 1u << i;
            DebugLog("[TrainingCharacter] PRESENTATION slot=%u char=%u source=%u face=%u color=%u cutMask=%u",
                     slot, character, presentation, images[0] != 0, images[1] != 0, cutMask);
        }
        changed = true;
        DebugLog("[TrainingCharacter] LOAD end side=%u char=%u moon=%u elapsedUs=%lld stage=%u",
                 side, choice.character, choice.moon, platform::RealMonotonicUs()-started, *CC_STAGE_SELECTOR_ADDR);
    }
    originalReset(battle);
}
bool Hook(uintptr_t address, const unsigned char* bytes, size_t length, void* replacement, void** original) {
    if (std::memcmp(reinterpret_cast<void*>(address), bytes, length)) return false;
    return MH_CreateHook(reinterpret_cast<void*>(address), replacement, original) == MH_OK &&
           cccaster::hook_batch::Enable(reinterpret_cast<void*>(address)) == MH_OK;
}
}
const Selection& Current() { return selection; }
bool Busy() { return selection.open || pending || training_palette::Active() || training_hitbox::Active(); }
const char* Error() { return error; }
int PortraitIndex(uint32_t character) {
    if (!installed || character >= 101) return -1;
    const auto* icons = reinterpret_cast<int*>(GameRuntime::Preferred(0x5897e8));
    // ボス差分に専用顔がない場合は元キャラの顔を使い、欄にBOSSを添える。
    return icons[character] >= 0 ? icons[character] : icons[CursorCharacter(character)];
}
int MoonPortraitIndex(uint32_t moon) {
    if (moon == 8 || moon == 9) return 3;
    // Steamのcsel_style00はC/F/Hの順。内部ムーン値0/1/2と一致する。
    return moon < 3 ? int(moon) : 0;
}
std::string CharacterName(uint32_t character) {
    auto* data = Descriptor(character);
    if (!data) return "?";
    const auto* name = reinterpret_cast<const char*>(data + 1);
    return std::string(name, strnlen(name, 32));
}
bool ReadImage(const char* path, std::vector<uint8_t>& bytes) {
    if (!game_build::RuntimeValidated()) return false;
    // Paletteは対戦/Versusでも利用するため、Trainingメニュー初期化には依存しない。
    uint32_t* file = nullptr;
    if (!cc_training_read_image(path,&file,GameRuntime::Preferred(0x519db0)) || !file)
        return false;
    ++file[0x48/4];
    auto* data = reinterpret_cast<uint8_t*>(file[0x30/4]);
    const auto size = cc_training_file_size(file);
    const bool valid = data && size >= 128 && size <= 16*1024*1024;
    if (valid) bytes.assign(data, data + size);
    if (data) reinterpret_cast<void (__cdecl*)(void*)>(GameRuntime::Preferred(0x52cf2d))(data);
    reinterpret_cast<void (__thiscall*)(void*)>(GameRuntime::Preferred(0x46b5a0))(file);
    reinterpret_cast<void (__cdecl*)(void*)>(GameRuntime::Preferred(0x52c38e))(file);
    return valid;
}
void ObserveMenu(uint32_t* menu, uint32_t* command) {
    if (!installed || *CC_GAME_MODE_ADDR != CC_GAME_MODE_IN_GAME || menu != menuSet ||
        mainMenu != *reinterpret_cast<uint32_t**>(GameRuntime::Preferred(0x7b4328))) return;
    if (pending) {
        if (restartDispatched) { *command = 0; return; }
        const auto restart = FindItem(menu, "RESTART");
        if (restart != UINT32_MAX) {
            menu[0x38/4] = restart; *command = 1; restartDispatched = true;
        }
        return;
    }
    if (selection.open || training_palette::Active() || training_hitbox::Active()) { *command = 0; return; }
    if (*command == 1 && menu[0x38/4] == FindItem(menu,"CC_HITBOX")) {
        training_hitbox::Open(); *command = 0; return;
    }
    if (*command == 1 && menu[0x38/4] == FindItem(menu,"CC_PALETTE")) {
        training_palette::Open(*reinterpret_cast<uint8_t*>(GameRuntime::Preferred(0x5c9c97)));
        *command = 0; return;
    }
    if (*command == 1 && menu[0x38/4] == 0) {
        selection.Open({Choice{*reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x7b4378)), *reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x7b4384))},
                        Choice{*reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x7b43a4)), *reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x7b43b0))}},
                       *reinterpret_cast<uint8_t*>(GameRuntime::Preferred(0x5c9c97)));
        suppressUntilRelease = true;
        error = "";
        *command = 0;
        DebugLog("[TrainingCharacter] OPEN side=%u char=%u moon=%u", selection.player, selection.choice.character, selection.choice.moon);
    }
}
}

namespace cccaster::game_interface {
bool RealGameMemory::ConfigureTrainingMenu() {
    using namespace training_character;
    if (installed) return true;
    if (!game_build::RuntimeValidated() || !ConfigureMenuObserver()) return false;
    for(const auto& signature : game_memory::steam_v15::TrainingMenu)
        if(!game_memory::steam_code::Matches(signature)) return false;
    for(const auto& signature : game_memory::steam_training_v158::Menu)
        if(!game_memory::steam_code::Matches(signature)) return false;
    if (!training_palette::Install() || !training_hitbox::Install()) return false;
    cc_training_presentation_loader=GameRuntime::Preferred(0x47d9d0);
    const unsigned char entry[]{0x55,0x8b,0xec,0x6a,0xff};
    if (!Hook(GameRuntime::Preferred(0x4d6320), entry, sizeof(entry), reinterpret_cast<void*>(Construct),
              reinterpret_cast<void**>(&originalConstructor)) ||
        !Hook(GameRuntime::Preferred(0x47a370), entry, sizeof(entry), reinterpret_cast<void*>(RoundReset),
              reinterpret_cast<void**>(&originalReset))) return false;
    installed = true;
    return true;
}
bool RealGameMemory::StepTrainingMenu(GameInput& p1, GameInput& p2, bool configuring) {
    using namespace training_character;
    training_hitbox::Step(p1,p2,configuring);
    training_palette::Step(p1,p2,configuring);
    const bool didChange = changed;
    changed = false;
    if (GameMode() != CC_GAME_MODE_IN_GAME) {
        selection.open = pending = false; mainMenu = menuSet = nullptr;
        suppressUntilRelease = false; previousButtons = previousDirection = 0;
        return didChange;
    }
    const auto buttons = p1.buttons | p2.buttons;
    const auto direction = p1.direction ? p1.direction : p2.direction;
    if (configuring && selection.open) { selection.open = false; suppressUntilRelease = true; }
    if (selection.open && !configuring && !suppressUntilRelease) {
        Action action = Action::None;
        const auto edge = buttons & ~previousButtons;
        if (edge & (CC_BUTTON_B | CC_BUTTON_CANCEL | CC_BUTTON_START)) action = Action::Cancel;
        else if (edge & (CC_BUTTON_A | CC_BUTTON_CONFIRM)) action = Action::Accept;
        else if (direction && (direction != previousDirection || platform::RealMonotonicUs() >= repeatAt)) {
            action = direction == 8 ? Action::Up : direction == 2 ? Action::Down :
                     direction == 4 ? Action::Left : direction == 6 ? Action::Right : Action::None;
            repeatAt = platform::RealMonotonicUs() + (direction == previousDirection ? 90000 : 350000);
        }
        const auto result = selection.Step(action);
        if (action != Action::None) error = "";
        if (result == Result::Apply) {
            if (!Available(selection.choice)) {
                selection.open = true; selection.field = Field::Moon;
                error = "This style is not available for this character.";
            } else if (selection.choice.character == selection.original[selection.player].character &&
                       selection.choice.moon == selection.original[selection.player].moon) {
                suppressUntilRelease = true;
            } else {
                pending = true; restartDispatched = false; suppressUntilRelease = true;
                DebugLog("[TrainingCharacter] COMMIT side=%u char=%u moon=%u", selection.player, selection.choice.character, selection.choice.moon);
            }
        } else if (result == Result::Cancelled) {
            suppressUntilRelease = true;
            DebugLog("[TrainingCharacter] CANCEL");
        }
    }
    previousButtons = buttons; previousDirection = direction;
    if (selection.open || pending || suppressUntilRelease || didChange) p1 = p2 = {};
    if (!buttons && !direction) suppressUntilRelease = false;
    return didChange;
}
}
