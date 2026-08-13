// ============================================================================
// Controller_Ui_Logic.cpp — コントローラーマッピング ロジック実装
// ============================================================================

#include "core_dll/ui/Controller_Ui_Logic.hpp"
#include "core_dll/hook/DirectInputHook.hpp"
#include "core_dll/common/DataPaths.hpp"
#include "cli_launcher/ConfigManager.hpp"
#include <imgui.h>

namespace cccaster::domain::ui {

// ============================================================================
// 共通ヘルパー
// ============================================================================

// 本体 ini のファイル名。パスは必ず paths::Resolve() を通す。
// 相対パス（"cccaster\\cccaster_v10.ini"）はゲームのカレントディレクトリ基準に
// なるため、DLL 基準で読む読み手（DirectInputHook / dllmain）と別の場所を指しうる。
static const char* const kMainConfigName = "cccaster_v10.ini";

static std::string SanitizeDeviceName(const std::string& name) {
    std::string s = name;
    for (char& c : s) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' || c == '>' || c == '|') c = '_';
    }
    return s;
}

static std::string GetDeviceNameById(int joyId) {
    if (joyId == -2) return "Keyboard";
    if (joyId >= 0) {
        auto devices = cccaster::game_interface::DirectInputHook::GetConnectedDevices();
        for (const auto& dev : devices) {
            if (dev.id == joyId) return dev.name;
        }
    }
    return "";
}

// config に保存されている（Sanitize 済みの）デバイス名から joyId を引く。
//
// 読み手側の同等処理は DirectInputHook.cpp の GetJoyIdFromDeviceName() だが
// static で公開されていない。公開 API を増やすより、公開済みの
// GetConnectedDevices() + 上の SanitizeDeviceName()（読み手の Sanitize 規則と
// 同一）で照合するほうが、読み手のファイルに手を入れずに済み衝突も起きない。
// ※ g_Controllers のインデックス == JoyDeviceInfo::id なので結果も一致する。
static int GetJoyIdByDeviceName(const std::string& sanitizedName) {
    if (sanitizedName.empty()) return -1;
    if (sanitizedName == "Keyboard") return -2;   // 読み手と同じ特別扱い
    for (const auto& dev : cccaster::game_interface::DirectInputHook::GetConnectedDevices()) {
        if (SanitizeDeviceName(dev.name) == sanitizedName) return dev.id;
    }
    return -1;   // 保存されていたデバイスが今は繋がっていない
}

// ============================================================================
// static メンバー定義
// ============================================================================

// -1 = 未割当。起動直後は必ず未割当から始まり、UI を開いた最初のフレームで
// BeginUiSession() が config から復元する（保存済み割当を消さないため）。
int         ControllerUiLogic::s_p1JoyId = -1;
int         ControllerUiLogic::s_p2JoyId = -1;
bool        ControllerUiLogic::s_uiSessionActive = false;
int         ControllerUiLogic::s_p1Position = 0;
int         ControllerUiLogic::s_p2Position = 0;
std::string ControllerUiLogic::s_p1Binds[NUM_GAME_INPUTS];
std::string ControllerUiLogic::s_p2Binds[NUM_GAME_INPUTS];
double      ControllerUiLogic::s_p1BindStartTime = 0.0;
double      ControllerUiLogic::s_p2BindStartTime = 0.0;
std::string ControllerUiLogic::s_p1CachedEdge;
std::string ControllerUiLogic::s_p2CachedEdge;

static const char* const s_gameInputNames[] = {
    "Up", "Down", "Left", "Right",
    "A (confirm)", "B (cancel)", "C", "D", "E",
    "Start", "FN1", "FN2", "A+B"
};

const char* const* ControllerUiLogic::GetGameInputNames() { return s_gameInputNames; }

// ============================================================================
// Getter 実装
// ============================================================================

int ControllerUiLogic::GetP1JoyId()  { return s_p1JoyId; }
int ControllerUiLogic::GetP2JoyId()  { return s_p2JoyId; }
int ControllerUiLogic::GetP1Position() { return s_p1Position; }
int ControllerUiLogic::GetP2Position() { return s_p2Position; }
const std::string* ControllerUiLogic::GetP1Binds() { return s_p1Binds; }
const std::string* ControllerUiLogic::GetP2Binds() { return s_p2Binds; }
const std::string& ControllerUiLogic::GetP1CachedEdge() { return s_p1CachedEdge; }
const std::string& ControllerUiLogic::GetP2CachedEdge() { return s_p2CachedEdge; }

// ============================================================================
// UI セッション（F4 で開いてから閉じるまで）の開始・終了
// ============================================================================

// 保存済みのデバイス割当を config から復元する。
//
// 【なぜ必要か】
//   s_pXJoyId は起動時 -1 固定で、config から復元する処理が無かった。
//   その状態で F4 を開閉すると SaveDeviceAllocations() が「未割当」を
//   そのまま書き出し、前回の割当が消えていた（＝設定画面を覗いただけで
//   コントローラが効かなくなる）。
//   復元と、下の「未割当では既存値を潰さない」は**両方**必要。
//   復元だけだと未接続時に消え、潰さないだけだと UI 上で前回値が見えない。
void ControllerUiLogic::RestoreDeviceAllocations() {
    using cccaster::main_app::ConfigManager;
    const int p1 = GetJoyIdByDeviceName(ConfigManager::GetString("Settings", "P1Device", ""));
    const int p2 = GetJoyIdByDeviceName(ConfigManager::GetString("Settings", "P2Device", ""));

    s_p1JoyId = p1;
    // 同じデバイスが両スロットに入ると、以降どちらを操作しても取り合いになる。
    // ini が壊れている場合の保険として P2 側を落とす。
    s_p2JoyId = (p2 != -1 && p2 == p1) ? -1 : p2;
}

void ControllerUiLogic::BeginUiSession() {
    if (s_uiSessionActive) return;
    s_uiSessionActive = true;
    // 開くたびに引き直す。前回の終了後にデバイスが抜き差しされていても
    // その時点の接続状況で解決できる。
    RestoreDeviceAllocations();
}

void ControllerUiLogic::EndUiSession() {
    // 【バグ修正】F4 で閉じたとき、途中まで割り当てたバインドを保存する。
    //   従来は ResetBindingState() が binds[] を捨てるだけで SaveBinds() を
    //   呼ばず、「Finish and Save」行まで到達しないと13キーが1つも保存され
    //   なかった。しかもデバイス割当だけは保存されるため、「デバイスは設定
    //   されているのに操作が効かない」という最悪の状態になっていた。
    //
    // 【破棄ではなく保存を選んだ理由】
    //   割り当てた本人にとって「押した内容が消える」ほうが損害が大きい。
    //   かつ SaveBinds() は空のバインド（＝このセッションで到達しなかった
    //   項目）を書かないので、途中保存しても残りの項目は前回値/既定値の
    //   ままになり、「半分だけ無反応」にはならない。
    if (s_p1Position > 0) SaveBinds(s_p1JoyId, "P1", s_p1Binds);
    if (s_p2Position > 0) SaveBinds(s_p2JoyId, "P2", s_p2Binds);
    if (s_p1Position > 0 || s_p2Position > 0) {
        cccaster::main_app::ConfigManager::Save(cccaster::core::paths::Resolve(kMainConfigName));
        cccaster::game_interface::DirectInputHook::ReloadConfigs();
    }
    s_uiSessionActive = false;
}

// ============================================================================
// デバイス割当保存
// ============================================================================

void ControllerUiLogic::SaveDeviceAllocations() {
    using cccaster::main_app::ConfigManager;

    const std::string p1Name = SanitizeDeviceName(GetDeviceNameById(s_p1JoyId));
    const std::string p2Name = SanitizeDeviceName(GetDeviceNameById(s_p2JoyId));

    // 未割当（空）のときの扱いが本修正の要点。
    //   従来は無条件に空文字を書いていたため、デバイスが未接続だっただけで
    //   前回の割当が消えていた。かといって常に残すと、デバイスを P1 から P2 へ
    //   移したときに古い P1Device が残り、次に開いたとき「両スロットが同じ
    //   デバイス」になって移動が無かったことにされる。
    //   そこで「そのデバイスが反対のスロットに入った」ことが確認できるときだけ
    //   消し、それ以外（＝単に未接続／未設定）は残す。
    auto saveSlot = [](const std::string& myName, const std::string& otherName, const std::string& prefix) {
        const std::string key = prefix + "Device";
        if (!myName.empty()) {
            ConfigManager::SetString("Settings", key, myName);
            return;
        }
        const std::string stored = ConfigManager::GetString("Settings", key, "");
        if (!stored.empty() && stored == otherName) {
            ConfigManager::SetString("Settings", key, "");   // 反対のスロットへ移動した
        }
        // else: 何も書かない（前回値を残す）
    };
    saveSlot(p1Name, p2Name, "P1");
    saveSlot(p2Name, p1Name, "P2");
    cccaster::main_app::ConfigManager::Save(cccaster::core::paths::Resolve(kMainConfigName));
    cccaster::game_interface::DirectInputHook::ReloadConfigs();
}

// ============================================================================
// バインド保存
// ============================================================================

void ControllerUiLogic::SaveBinds(int joyId, const std::string& prefix, std::string* binds) {
    // 未割当のまま保存すると "UnknownDevice_-1.ini" という実体のない
    // ファイルを作り、P1Device をその名前で固定してしまう。何もしない。
    if (joyId == -1) return;

    std::string deviceName = GetDeviceNameById(joyId);
    if (deviceName.empty()) deviceName = "UnknownDevice_" + std::to_string(joyId);

    const std::string sanitizedName = SanitizeDeviceName(deviceName);
    // 読み手（DirectInputHook::GetDeviceFileName）と同じ解決関数を通す。
    // Sanitize 規則も読み手と一致していなければならない（変更禁止）。
    const std::string filename = cccaster::core::paths::Resolve(sanitizedName + ".ini");
    cccaster::main_app::Config deviceConfig;
    deviceConfig.Load(filename);

    static const char* const kMappingKeys[NUM_GAME_INPUTS] = {
        "Up", "Down", "Left", "Right",
        "A", "B", "C", "D", "E",
        "Start", "FN1", "FN2", "A+B"
    };
    for (int i = 0; i < NUM_GAME_INPUTS; ++i) {
        // 空 = このセッションで到達しなかった項目。空文字で上書きすると
        // 読み手の既定値フォールバックまで殺してそのボタンが無反応になるため、
        // 既存の値を残す。
        // ※「Finish and Save」まで進んだ場合は仕様上13項目すべて非空になる
        //   （pos はバインドが入ったときだけ進む）ので、通常保存には影響しない。
        if (binds[i].empty()) continue;
        deviceConfig.SetString("Mapping", kMappingKeys[i], binds[i]);
    }

    // 【バグ4への対応 — 部分対応】
    //   読み手（DirectInputHook::BuildPlayerInput）は Up_Alt/Down_Alt/
    //   Left_Alt/Right_Alt を既定値 A1-/A1+/A0-/A0+ で参照しており、
    //   アナログスティックが常時バインドされている。UI に該当項目が無いので
    //   利用者からは解除も変更もできない（スティックのドリフトが誤入力になる）。
    //
    //   UI 項目を13→17に増やす案は採らなかった。このウィザードは項目を
    //   スキップできず（バインドが入るまで pos が進まない）、17項目にすると
    //   スティックの無いレバー/パッドで設定を完了できなくなるため。
    //   読み手から消す案も採らなかった。既定でスティックが動く利点を失ううえ、
    //   UI 側に代替の設定手段が無いので単なる機能後退になる。
    //
    //   代わりに、隠れていた既定値を ini に実体として書き出す。値がファイル上に
    //   見えるので、利用者は該当行を空にすればアナログを解除でき、別の軸にも
    //   変更できる。既に値がある場合（利用者が編集済み）は触らない。
    //   キーボード（joyId == -2）には軸が無いので書かない。
    if (joyId >= 0) {
        static const char kAbsent[] = "\x01__absent__";   // 「未設定」と「空文字」の区別用
        const struct { const char* key; const char* def; } kAltDefaults[] = {
            { "Up_Alt",    "A1-" },
            { "Down_Alt",  "A1+" },
            { "Left_Alt",  "A0-" },
            { "Right_Alt", "A0+" },
        };
        for (const auto& alt : kAltDefaults) {
            if (deviceConfig.GetString("Mapping", alt.key, kAbsent) == kAbsent)
                deviceConfig.SetString("Mapping", alt.key, alt.def);
        }
    }

    deviceConfig.Save(filename);
    cccaster::main_app::ConfigManager::SetString("Settings", prefix + "Device", sanitizedName);
}

// ============================================================================
// デバイス選択入力処理
// ============================================================================

void ControllerUiLogic::ProcessDeviceSelectionInput() {
    int activeJoyId = -1;
    int dir = cccaster::game_interface::DirectInputHook::GetActiveDeviceDirection(activeJoyId);

    if (s_p1Position == 0 && dir == -1 && activeJoyId >= 0) {
        if (s_p2JoyId == activeJoyId) { if (s_p2Position == 0) s_p2JoyId = -1; }
        else if (s_p1JoyId != activeJoyId) s_p1JoyId = activeJoyId;
    }
    else if (s_p2Position == 0 && dir == 1 && activeJoyId >= 0) {
        if (s_p1JoyId == activeJoyId) { if (s_p1Position == 0) s_p1JoyId = -1; }
        else if (s_p2JoyId != activeJoyId) s_p2JoyId = activeJoyId;
    }

    if (s_p1Position == 0 && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
        if (s_p2JoyId == -2) { if (s_p2Position == 0) s_p2JoyId = -1; }
        else if (s_p1JoyId != -2) s_p1JoyId = -2;
    }
    if (s_p2Position == 0 && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
        if (s_p1JoyId == -2) { if (s_p1Position == 0) s_p1JoyId = -1; }
        else if (s_p2JoyId != -2) s_p2JoyId = -2;
    }
}

// ============================================================================
// バインド入力処理
// ============================================================================

void ControllerUiLogic::ProcessBindingInput(int joyId, int playerIndex, int& pos, std::string* binds) {
    if (pos == 0) return;

    double bindStartTime = (playerIndex == 0) ? s_p1BindStartTime : s_p2BindStartTime;
    if (ImGui::GetTime() - bindStartTime < MAPPING_START_DEAD_TIME) return;

    int maxPos = NUM_GAME_INPUTS + 1;
    int bindIndex = pos - 1;

    bool deleteBind = ImGui::IsKeyPressed(ImGuiKey_Delete, false);
    std::string newBind = "";

    if (joyId == -2) {
        if (!deleteBind && !ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
                if (ImGui::IsKeyPressed((ImGuiKey)key, false)) {
                    if (pos == maxPos && (key == ImGuiKey_Enter || key == ImGuiKey_Space)) {
                        SaveBinds(joyId, playerIndex == 0 ? "P1" : "P2", binds);
                        cccaster::main_app::ConfigManager::Save(cccaster::core::paths::Resolve(kMainConfigName));
                        cccaster::game_interface::DirectInputHook::ReloadConfigs();
                        pos = 0; return;
                    }
                    if (key != ImGuiKey_Enter && key != ImGuiKey_F4 && key != ImGuiKey_Escape) {
                        std::string kName = ImGui::GetKeyName((ImGuiKey)key);
                        if (kName.find("Gamepad") == std::string::npos && kName.find("Mouse") == std::string::npos) {
                            newBind = kName; break;
                        }
                    }
                }
            }
        }
    } else if (joyId >= 0) {
        const std::string& edge = (playerIndex == 0) ? s_p1CachedEdge : s_p2CachedEdge;
        if (!edge.empty()) {
            if (pos == maxPos && edge.find("H") == std::string::npos && edge.find("A") == std::string::npos) {
                SaveBinds(joyId, playerIndex == 0 ? "P1" : "P2", binds);
                cccaster::main_app::ConfigManager::Save(cccaster::core::paths::Resolve(kMainConfigName));
                cccaster::game_interface::DirectInputHook::ReloadConfigs();
                pos = 0; return;
            }
            if (pos < maxPos) newBind = edge;
        }
    }

    if (deleteBind && pos < maxPos) binds[bindIndex] = "";
    if (!newBind.empty() && pos < maxPos) { binds[bindIndex] = newBind; pos++; }
}

// ============================================================================
// メインロジック更新（毎フレーム）
// ============================================================================

void ControllerUiLogic::Update() {
    ProcessDeviceSelectionInput();

    // バインド開始共通ヘルパー
    auto startBinding = [](int& pos, double& startTime, std::string* binds) {
        if (pos == 0) {
            pos = 1;
            startTime = ImGui::GetTime();
            for (int i = 0; i < NUM_GAME_INPUTS; ++i) binds[i] = "";
        }
    };

    bool kbdStart = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false);
    bool bindingJustStarted = false;
    if (kbdStart) {
        if (s_p1JoyId == -2 && s_p1Position == 0) { startBinding(s_p1Position, s_p1BindStartTime, s_p1Binds); bindingJustStarted = true; }
        if (s_p2JoyId == -2 && s_p2Position == 0) { startBinding(s_p2Position, s_p2BindStartTime, s_p2Binds); bindingJustStarted = true; }
    }

    // Edge 取得 + バインド開始 + 切断検知
    auto devices = cccaster::game_interface::DirectInputHook::GetConnectedDevices();
    s_p1CachedEdge.clear();
    s_p2CachedEdge.clear();
    bool p1DeviceStillExists = (s_p1JoyId == -2);
    bool p2DeviceStillExists = (s_p2JoyId == -2);
    for (const auto& dev : devices) {
        if (dev.id == s_p1JoyId) p1DeviceStillExists = true;
        if (dev.id == s_p2JoyId) p2DeviceStillExists = true;

        std::string edge = cccaster::game_interface::DirectInputHook::GetAnyInputEdge(dev.id);
        if (!edge.empty()) {
            if (dev.id == s_p1JoyId) s_p1CachedEdge = edge;
            if (dev.id == s_p2JoyId) s_p2CachedEdge = edge;

            if (edge.find("H") == std::string::npos && edge.find("A") == std::string::npos) {
                if (s_p1JoyId == dev.id) startBinding(s_p1Position, s_p1BindStartTime, s_p1Binds);
                if (s_p2JoyId == dev.id) startBinding(s_p2Position, s_p2BindStartTime, s_p2Binds);
            }
        }
    }
    if (!p1DeviceStillExists && s_p1Position > 0) { s_p1Position = 0; s_p1JoyId = -1; }
    if (!p2DeviceStillExists && s_p2Position > 0) { s_p2Position = 0; s_p2JoyId = -1; }

    // バインド入力処理（開始フレームはスキップ — デッドタイムで保護）
    if (!bindingJustStarted) {
        ProcessBindingInput(s_p1JoyId, 0, s_p1Position, s_p1Binds);
        ProcessBindingInput(s_p2JoyId, 1, s_p2Position, s_p2Binds);
    }
}

// ============================================================================
// バインド状態リセット
// ============================================================================

void ControllerUiLogic::ResetBindingState() {
    if (s_p1Position > 0) {
        s_p1Position = 0;
        for (int i = 0; i < NUM_GAME_INPUTS; ++i) s_p1Binds[i] = "";
    }
    if (s_p2Position > 0) {
        s_p2Position = 0;
        for (int i = 0; i < NUM_GAME_INPUTS; ++i) s_p2Binds[i] = "";
    }
}

} // namespace cccaster::domain::ui
