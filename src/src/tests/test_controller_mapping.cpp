#include "core_dll/ui/ControllerMappingValidation.hpp"
#include "core_dll/engine/TrainingState.hpp"
#include "test_support.hpp"
#include <cstring>
#include <fstream>
#include <filesystem>
#include <map>
#ifdef _WIN32
#include "core_dll/ui/Controller_Ui_Logic.hpp"
#include "core_dll/common/DataPaths.hpp"
#include "cli_launcher/ConfigManager.hpp"
#include <imgui.h>
#endif
using namespace cccaster::input;
using namespace cccaster::domain::ui;
using namespace cccaster::domain::session;

static std::vector<cccaster::game_interface::JoyDeviceInfo> devices;
static cccaster::game_interface::JoyDeviceInfo Device(int id, const char *name, const char *guid) {
    cccaster::game_interface::JoyDeviceInfo d{};
    d.id = id; std::strncpy(d.name, name, sizeof(d.name)-1); std::strncpy(d.instanceGuid, guid, sizeof(d.instanceGuid)-1);
    return d;
}
struct Memory : cccaster::game_interface::IGameMemory {
    int value = 0, saves = 0, loads = 0, recordingRestarts = 0;
    bool saveOK = true, loadOK = true, restartOK = true; uint8_t intro = 0;
    int16_t enemyStatus = 0;
    bool IsTrainingDummy() const override { return cccaster::IsDummyEnemyStatus(enemyStatus); }
    bool IsTrainingRecording() const override { return enemyStatus == -1; }
    bool RestartTrainingRecording() override { ++recordingRestarts; return restartOK; }
    bool IsAvailable() const override { return true; }
    uint32_t GameMode() const override { return 20; }
    uint8_t IntroState() const override { return intro; }
    uint32_t WorldTimer() const override { return 0; }
    uint32_t RealTimer() const override { return 0; }
    uint32_t MenuStateCounter() const override { return 0; }
    void WriteInput(cccaster::game_interface::GameInput, cccaster::game_interface::GameInput) override {}
    bool SupportsSnapshots() const override { return true; }
    size_t SnapshotSize() const override { return sizeof(value); }
    bool SaveSnapshot(std::span<char> out) override { ++saves; std::memcpy(out.data(), &value, sizeof(value)); return saveOK; }
    bool LoadSnapshot(std::span<char> in) override { ++loads; if (loadOK) std::memcpy(&value, in.data(), sizeof(value)); return loadOK; }
};
#ifdef _WIN32
static std::map<int,std::string> edges, heldInputs;
static int reloads = 0;
namespace cccaster::game_interface {
std::vector<JoyDeviceInfo> DirectInputHook::GetConnectedDevices() { return devices; }
std::string DirectInputHook::GetAnyInputEdge(int id) { return edges[id]; }
bool DirectInputHook::IsBindingPressed(int id, const std::string &bind) { return !bind.empty() && heldInputs[id] == bind; }
void DirectInputHook::ReloadConfigs() { ++reloads; }
}
static std::string Read(const std::string &path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
static void DeviceFrame(std::map<int,std::string> input={}, std::map<int,std::string> down={}) {
    edges=std::move(input); heldInputs=std::move(down);
    ImGui::GetIO().DeltaTime = 0.25f;
    ImGui::NewFrame(); ControllerUiLogic::Update(); ImGui::Render();
}
static void Frame(const std::string &input = "", const std::string &down = "") {
    const int id=ControllerUiLogic::DeviceId(0);
    DeviceFrame({{id,input}},{{id,down}});
}
static void Bind(int index, const std::string &value) {
    ControllerUiLogic::StartBinding(0, index); Frame(); Frame(value,value); Frame();
}
static void TestUi() {
    using L=ControllerUiLogic;
    using cccaster::main_app::Config;
    using cccaster::main_app::ConfigManager;
    const auto root=std::filesystem::temp_directory_path() / ("cccaster_legacy_mapping_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root);
    cccaster::core::paths::SetDataRoot(root.string());
    const auto mainPath=(root/"cccaster_steam.ini").string(), legacyPath=(root/"Pad.ini").string();
    const auto p1Path=(root/"Pad__AAA.ini").string(), p2Path=(root/"Pad__BBB.ini").string();
    Config main,profile;
    main.SetString("Settings","Unrelated","keep"); main.Save(mainPath);
    const auto defaults=DefaultBindings(false);
    for(int i=0;i<BindingCount;++i) profile.SetString("Mapping",BindingKeys[i],defaults[i]);
    profile.SetString("Other","UserValue","keep");
    profile.SetString("Mapping","Up_Alt","A3-");
    profile.SetString("Mapping","TrainingSave","B20"); profile.Save(legacyPath);
    const auto legacyBefore=Read(legacyPath);
    ConfigManager::Load(mainPath);
    devices={Device(0,"Pad","AAA"),Device(1,"Pad","BBB")};
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.DisplaySize=ImVec2(640,480); io.IniFilename=nullptr;
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    const auto key=[&](ImGuiKey value,bool down) { io.AddKeyEvent(value,down); DeviceFrame(); };
    const auto navigateTo=[&](int p,int row) { for(int n=0;n<16 && L::SelectedRow(p)!=row;++n) L::Navigate(p,1); };

    CC_CASE("opening and closing do not change settings");
    const auto untouched=Read(mainPath);
    L::BeginUiSession(); CC_CHECK(L::Device(0).Empty()); CC_CHECK(L::EndUiSession());
    CC_CHECK(Read(mainPath)==untouched); CC_CHECK_EQ(reloads,0);

    CC_CASE("left/right joins two identical pads and saves allocation immediately");
    L::BeginUiSession();
    DeviceFrame({{0,"H0_4"},{1,"H0_6"}});
    CC_CHECK_EQ(L::DeviceId(0),0); CC_CHECK_EQ(L::DeviceId(1),1);
    CC_CHECK(ConfigManager::GetString("Settings","P1DeviceGuid")=="AAA");
    CC_CHECK(ConfigManager::GetString("Settings","P2DeviceGuid")=="BBB");
    CC_CHECK(!L::SelectDevice(0,1));
    Frame("B0","B0"); Frame(); CC_CHECK_EQ(L::SelectedRow(0),L::OverviewRow);

    CC_CASE("pad down selects A; up from the device name selects Done");
    L::Navigate(0,-1); CC_CHECK_EQ(L::SelectedRow(0),L::DoneRow);
    L::Navigate(0,1); CC_CHECK_EQ(L::SelectedRow(0),L::OverviewRow);
    DeviceFrame({{0,"H0_2"},{1,"H0_2"}});
    CC_CHECK_EQ(L::CaptureBinding(0),4); CC_CHECK_EQ(L::CaptureBinding(1),4);

    CC_CASE("each pad commits on release independently and saves before closing");
    DeviceFrame({{0,"B20"},{1,"B21"}},{{0,"B20"},{1,"B21"}});
    CC_CHECK(L::Binds(0)[4]=="B0"); CC_CHECK(L::Binds(1)[4]=="B0");
    DeviceFrame({},{{1,"B21"}});
    CC_CHECK(L::Binds(0)[4]=="B20"); CC_CHECK(L::Binds(1)[4]=="B0");
    Config check; check.Load(p1Path);
    CC_CHECK(check.GetString("Mapping","A")=="B20");
    CC_CHECK(check.GetString("Mapping","TrainingSave")=="B20");
    CC_CHECK(check.GetString("Mapping","Up_Alt")=="A3-");
    CC_CHECK(check.GetString("Other","UserValue")=="keep");
    CC_CHECK_EQ(L::CaptureBinding(0),5); CC_CHECK_EQ(L::CaptureBinding(1),4);
    DeviceFrame(); CC_CHECK(L::Binds(1)[4]=="B21");
    CC_CHECK(Read(legacyPath)==legacyBefore);

    CC_CASE("reusing a button removes its old assignment instead of blocking save");
    Bind(5,"B20"); CC_CHECK(L::Binds(0)[4].empty()); CC_CHECK(L::Binds(0)[5]=="B20");
    check.Load(p1Path);
    CC_CHECK(check.GetString("Mapping","A","missing").empty());
    CC_CHECK(check.GetString("Mapping","B")=="B20");
    CC_CHECK(!L::StatusError());

    CC_CASE("outward clears any selected action and inward releases an edited device");
    L::StartBinding(0,5); Frame("H0_4");
    CC_CHECK(L::Binds(0)[5].empty()); CC_CHECK_EQ(L::CaptureBinding(0),-1);
    check.Load(p1Path); CC_CHECK(check.GetString("Mapping","B","missing").empty());
    Frame("H0_6"); CC_CHECK(L::Device(0).Empty());
    DeviceFrame({{0,"H0_4"}}); CC_CHECK_EQ(L::DeviceId(0),0);
    CC_CHECK(L::Binds(0)[5].empty()); CC_CHECK(L::Binds(1)[4]=="B21");

    CC_CASE("last binding stays selected until the player moves to Done");
    Bind(12,"B23"); CC_CHECK_EQ(L::SelectedRow(0),12); CC_CHECK_EQ(L::CaptureBinding(0),12);
    Frame("H0_2"); CC_CHECK_EQ(L::SelectedRow(0),L::DoneRow);
    Frame("B0","B0"); CC_CHECK_EQ(L::SelectedRow(0),L::OverviewRow);
    CC_CHECK(!L::TakeCloseRequest()); // P2 is still editing.
    navigateTo(1,L::DoneRow); DeviceFrame({{1,"B0"}},{{1,"B0"}});
    CC_CHECK(L::TakeCloseRequest()); CC_CHECK(L::EndUiSession());

    CC_CASE("Done and a simultaneous other-player edit do not close the overlay");
    L::BeginUiSession(); L::Navigate(0,-1);
    DeviceFrame({{0,"B0"},{1,"H0_2"}},{{0,"B0"}});
    CC_CHECK_EQ(L::SelectedRow(1),4); CC_CHECK(!L::TakeCloseRequest());
    CC_CHECK(L::EndUiSession());

    CC_CASE("disconnect and reorder never read an identical device's input");
    L::BeginUiSession(); const auto stored=L::Binds(0)[4];
    L::StartBinding(0,4); devices={Device(0,"Pad","BBB")}; DeviceFrame({{0,"B5"}},{{0,"B5"}});
    CC_CHECK_EQ(L::DeviceId(0),-1); CC_CHECK_EQ(L::CaptureBinding(0),-1);
    CC_CHECK(L::Binds(0)[4]==stored);
    devices={Device(0,"Pad","BBB"),Device(1,"Pad","AAA")};
    CC_CHECK_EQ(L::DeviceId(0),1);
    CC_CHECK(L::EndUiSession());

    CC_CASE("write failure leaves the old binding and file intact; a later assignment retries");
    L::BeginUiSession(); const auto saved=Read(p1Path), previous=L::Binds(0)[4];
    cccaster::core::paths::SetDataRoot((root/"missing").string());
    Bind(4,"B24"); CC_CHECK(L::StatusError()); CC_CHECK(L::Binds(0)[4]==previous);
    CC_CHECK(Read(p1Path)==saved);
    cccaster::core::paths::SetDataRoot(root.string());
    Bind(4,"B24"); CC_CHECK(!L::StatusError()); CC_CHECK(L::Binds(0)[4]=="B24");
    L::EndUiSession();

    CC_CASE("suspending keeps committed inputs but discards an unfinished press");
    L::BeginUiSession(); L::StartBinding(0,5); Frame("B25","B25");
    L::Suspend(); L::BeginUiSession();
    CC_CHECK(L::Binds(0)[4]=="B24"); CC_CHECK(L::Binds(0)[5].empty());

    CC_CASE("keyboard direction requires Enter release; key-down commits and auto-advances");
    CC_CHECK(L::SelectDevice(1,-1));
    key(ImGuiKey_RightArrow,true); key(ImGuiKey_RightArrow,false);
    CC_CHECK_EQ(L::DeviceId(1),-2);
    key(ImGuiKey_DownArrow,true); key(ImGuiKey_DownArrow,false);
    CC_CHECK_EQ(L::SelectedRow(1),0); CC_CHECK_EQ(L::CaptureBinding(1),-1);
    key(ImGuiKey_Enter,true); DeviceFrame(); CC_CHECK(L::Binds(1)[0]=="I");
    key(ImGuiKey_Enter,false); key(ImGuiKey_UpArrow,true);
    CC_CHECK_EQ(L::DeviceId(1),-2); CC_CHECK(L::Binds(1)[0]=="UpArrow"); CC_CHECK_EQ(L::CaptureBinding(1),1);
    DeviceFrame(); CC_CHECK_EQ(L::CaptureBinding(1),1); // Held arrow cannot fill the next direction.
    key(ImGuiKey_UpArrow,false); key(ImGuiKey_DownArrow,true); key(ImGuiKey_DownArrow,false);
    key(ImGuiKey_LeftArrow,true); key(ImGuiKey_LeftArrow,false);
    key(ImGuiKey_RightArrow,true); key(ImGuiKey_RightArrow,false);
    CC_CHECK_EQ(L::DeviceId(1),-2); CC_CHECK_EQ(L::CaptureBinding(1),4);
    key(ImGuiKey_X,true); CC_CHECK(L::Binds(1)[4]=="X"); key(ImGuiKey_X,false);
    navigateTo(1,L::DoneRow); key(ImGuiKey_Enter,true); key(ImGuiKey_Enter,false);
    CC_CHECK(L::TakeCloseRequest()); CC_CHECK(L::EndUiSession());
    CC_CHECK(Read(legacyPath)==legacyBefore);
    CC_CHECK(ConfigManager::GetString("Settings","Unrelated")=="keep");
    ImGui::DestroyContext();
    std::printf("Fixtures: %s\n",root.string().c_str());
}
#endif

int main() {
    CC_CASE("required controls and duplicate validation");
    auto binds = DefaultBindings(false); CC_CHECK(ValidateControllerMapping(binds).valid);
    binds[0].clear(); CC_CHECK(!ValidateControllerMapping(binds).valid);
    binds = DefaultBindings(false); binds[1]=binds[0]; CC_CHECK(!ValidateControllerMapping(binds).valid);
    binds.fill("B1"); CC_CHECK(!ValidateControllerMapping(binds).valid);
    binds = DefaultBindings(false); binds[10].clear(); binds[11].clear(); binds[12].clear();
    CC_CHECK(ValidateControllerMapping(binds).valid);
    binds[13]=binds[14]=binds[4]; CC_CHECK(ValidateControllerMapping(binds).valid);
    CC_CASE("identity strict GUID and legacy unique-name migration");
    devices = {Device(0,"Pad","BBB"),Device(1,"Pad","AAA")};
    CC_CHECK_EQ(ResolveDevice({"Pad","AAA"},devices),1);
    CC_CHECK_EQ(ResolveDevice({"Pad",""},devices),-1);
    devices.pop_back(); CC_CHECK_EQ(ResolveDevice({"Pad","AAA"},devices),-1);
    CC_CHECK_EQ(ResolveDevice({"Pad",""},devices),0);
    CC_CASE("FN1 save/hold and FN2 reset-completion load");
    Memory mem; TrainingState state;
    cccaster::TrainingFrameSample sample; sample.valid=true; sample.round=1;
    sample.trueFrame=sample.simulationFrame=100;
    int64_t time=100;
    auto step=[&](int buttons,int mode=1,bool battle=true,bool configuring=false) {
        return state.Step(mode,battle,configuring,buttons,sample,mem,++time);
    };
    for(int mode : {0,2,3,4,255}) { step(0,mode); step(1,mode); step(2,mode); }
    CC_CHECK_EQ(mem.saves,0); CC_CHECK_EQ(mem.loads,0);
    step(0); CC_CHECK(step(2)==TrainingStateEvent::None); CC_CHECK(state.AllowResetInput());
    step(0); mem.value=42; CC_CHECK(step(1)==TrainingStateEvent::Saved); CC_CHECK(state.Holding());
    CC_CHECK(state.Notice(time+5000000)==TrainingStateEvent::Holding);
    const int saved=mem.saves; step(1); CC_CHECK_EQ(mem.saves,saved);
    step(0); CC_CHECK(!state.Holding());
    // FN2なしでは、初期位置や時計の変化があっても読込まない。
    sample.simulationFrame=0; mem.value=99; step(0); CC_CHECK_EQ(mem.loads,0);
    sample.simulationFrame=100;
    CC_CHECK(step(2)==TrainingStateEvent::None); CC_CHECK(state.AllowResetInput()); CC_CHECK_EQ(mem.loads,0);
    step(2); CC_CHECK(!state.AllowResetInput()); CC_CHECK_EQ(mem.loads,0);
    sample.simulationFrame=0;
    CC_CHECK(step(2)==TrainingStateEvent::Loaded); CC_CHECK_EQ(mem.value,42);
    const int loaded=mem.loads; step(2); CC_CHECK_EQ(mem.loads,loaded); CC_CHECK(!state.AllowResetInput());
    step(0); sample.simulationFrame=100;
    CC_CASE("FN2 waits through reset intro and wins over FN1");
    step(3); CC_CHECK(!state.Holding()); CC_CHECK_EQ(mem.saves,saved);
    sample.paused=true; step(2); sample.paused=false; // 通常FN2の一時停止を挟む。
    sample.valid=false; mem.intro=1; step(2); CC_CHECK(state.HasState());
    sample.valid=true; mem.intro=0;
    CC_CHECK(step(2)==TrainingStateEvent::Loaded); CC_CHECK_EQ(mem.loads,loaded+1);
    step(2); CC_CHECK_EQ(mem.loads,loaded+1);
    CC_CASE("save failure retains previous slot without freezing");
    step(0); mem.saveOK=false; mem.value=15;
    CC_CHECK(step(1)==TrainingStateEvent::SaveFailed); CC_CHECK(state.HasState()); CC_CHECK(!state.Holding());
    step(0); step(2); sample.trueFrame=0;
    CC_CHECK(step(0)==TrainingStateEvent::Loaded); CC_CHECK_EQ(mem.value,42);
    sample.trueFrame=100; mem.saveOK=true;
    CC_CASE("hitstop save freezes once and release resumes; F4/pause disarm");
    sample.stopped=true; sample.playerStopped[0]=sample.playerStopped[1]=true;
    step(0); CC_CHECK(step(1)==TrainingStateEvent::Saved); CC_CHECK(state.Holding());
    step(0); CC_CHECK(!state.Holding());
    sample.globalFreeze=true; step(1); CC_CHECK(state.Holding());
    for (bool configuring : {false,true}) {
        sample.paused=!configuring;
        const int saves=mem.saves, loads=mem.loads;
        step(1,1,true,configuring); CC_CHECK(!state.Holding());
        step(0,1,true,configuring); step(2,1,true,configuring);
        CC_CHECK_EQ(mem.saves,saves); CC_CHECK_EQ(mem.loads,loads);
        sample.paused=false; step(2); CC_CHECK(!state.AllowResetInput());
        step(0); CC_CHECK(step(1)==TrainingStateEvent::Saved);
    }
    CC_CASE("aborted reset expires, leaving battle clears and held buttons do not save");
    step(0); step(2); time+=10000001; step(0);
    const int loads=mem.loads; sample.trueFrame=0; step(0); CC_CHECK_EQ(mem.loads,loads);
    step(0,1,false); CC_CHECK(!state.HasState()); CC_CHECK(!state.Holding());
    step(1); CC_CHECK(!state.HasState()); step(0); step(1); CC_CHECK(state.HasState());
    step(0); sample.round++; step(0); CC_CHECK(state.HasState());
    step(1); step(0); sample.activeCharacter[0]=2; step(0); CC_CHECK(state.HasState());
    CC_CASE("failed reset load retains state and can retry");
    step(1); step(0); sample.trueFrame=100; step(2); sample.trueFrame=0; mem.loadOK=false;
    CC_CHECK(step(0)==TrainingStateEvent::LoadFailed); CC_CHECK(state.HasState());
    CC_CHECK(state.Notice(time+4000001)==TrainingStateEvent::None);
    mem.loadOK=true; sample.trueFrame=100; step(0); step(2); sample.trueFrame=0;
    CC_CHECK(step(0)==TrainingStateEvent::Loaded); CC_CHECK(state.HasState());
    CC_CASE("only DUMMY and recording status use direct FN2 load");
    for (int16_t status : {int16_t(0), int16_t(1), int16_t(2), int16_t(3), int16_t(4), int16_t(6)})
        CC_CHECK(!cccaster::IsDummyEnemyStatus(status));
    for (int16_t status : {int16_t(5), int16_t(-1)}) {
        state.Reset(); mem.enemyStatus=status; mem.loadOK=mem.saveOK=true;
        sample.valid=true; sample.paused=false; sample.trueFrame=sample.simulationFrame=100;
        step(0); const int beforeEmpty=mem.loads;
        CC_CHECK(step(2)==(status == -1 ? TrainingStateEvent::RecordingRestarted : TrainingStateEvent::Empty));
        CC_CHECK(!state.AllowResetInput());
        CC_CHECK_EQ(mem.loads,beforeEmpty);
        step(0); mem.value=42; CC_CHECK(step(1)==TrainingStateEvent::Saved); step(0);
        mem.value=99; const int beforeLoad=mem.loads;
        CC_CHECK(step(2)==TrainingStateEvent::Loaded); CC_CHECK_EQ(mem.value,42);
        CC_CHECK_EQ(mem.loads,beforeLoad+1); CC_CHECK(!state.AllowResetInput());
        for (int i=0;i<12;++i) step(2);
        CC_CHECK_EQ(mem.loads,beforeLoad+1);
        // ゲームの時計やイントロが変わらなくても、解放後の再押下で復元できる。
        step(0); mem.value=87; CC_CHECK(step(2)==TrainingStateEvent::Loaded); CC_CHECK_EQ(mem.value,42);
        step(0); sample.paused=true; const int beforeMenu=mem.loads;
        step(2); CC_CHECK_EQ(mem.loads,beforeMenu); sample.paused=false; step(2);
        CC_CHECK_EQ(mem.loads,beforeMenu); step(0); CC_CHECK(step(2)==TrainingStateEvent::Loaded);
        step(0); mem.loadOK=false; CC_CHECK(step(2)==TrainingStateEvent::LoadFailed); CC_CHECK(state.HasState());
        step(0); mem.loadOK=true; mem.value=81;
        CC_CHECK(step(2)==TrainingStateEvent::Loaded); CC_CHECK_EQ(mem.value,42);
    }
    CC_CASE("one saved state can cross between normal and DUMMY enemy settings");
    state.Reset(); mem.enemyStatus=0; mem.loadOK=mem.saveOK=true;
    sample.valid=true; sample.paused=false; sample.trueFrame=sample.simulationFrame=100;
    step(0); mem.value=123; CC_CHECK(step(1)==TrainingStateEvent::Saved); step(0);
    mem.enemyStatus=5; mem.value=456;
    CC_CHECK(step(2)==TrainingStateEvent::Loaded); CC_CHECK_EQ(mem.value,123);
    CC_CHECK_EQ(mem.enemyStatus,5); CC_CHECK(!state.AllowResetInput()); CC_CHECK(state.HasState());
    step(0); mem.value=789; CC_CHECK(step(1)==TrainingStateEvent::Saved); step(0);
    mem.enemyStatus=0; mem.value=456;
    CC_CHECK(step(2)==TrainingStateEvent::None); CC_CHECK(state.AllowResetInput());
    sample.simulationFrame=0;
    CC_CHECK(step(0)==TrainingStateEvent::Loaded); CC_CHECK_EQ(mem.value,789);
    CC_CHECK_EQ(mem.enemyStatus,0); CC_CHECK(state.HasState());
    // 通常設定で読込んだ後も、再保存なしでDUMMYへ戻して再利用する。
    mem.enemyStatus=5; mem.value=456;
    CC_CHECK(step(2)==TrainingStateEvent::Loaded); CC_CHECK_EQ(mem.value,789);
    step(0); mem.enemyStatus=-1; mem.value=456;
    CC_CHECK(step(2)==TrainingStateEvent::Loaded); CC_CHECK_EQ(mem.value,789);
    CC_CHECK_EQ(mem.enemyStatus,-1); CC_CHECK(!state.AllowResetInput());
    CC_CASE("recording without a save restarts once per FN2 press and respects menus");
    state.Reset(); mem.enemyStatus=-1; mem.value=345;
    step(0); const int restarts=mem.recordingRestarts;
    CC_CHECK(step(2)==TrainingStateEvent::RecordingRestarted);
    CC_CHECK_EQ(mem.recordingRestarts,restarts+1); CC_CHECK_EQ(mem.value,345);
    CC_CHECK(!state.HasState()); CC_CHECK(!state.AllowResetInput());
    for (int i=0;i<12;++i) step(2);
    CC_CHECK_EQ(mem.recordingRestarts,restarts+1);
    step(0); sample.paused=true; step(2); sample.paused=false; step(2);
    CC_CHECK_EQ(mem.recordingRestarts,restarts+1);
    step(0); step(2,1,true,true); step(2);
    CC_CHECK_EQ(mem.recordingRestarts,restarts+1);
    step(0); mem.restartOK=false;
    CC_CHECK(step(2)==TrainingStateEvent::RecordingRestartFailed); CC_CHECK(!state.HasState());
    step(0); mem.restartOK=true;
    CC_CHECK(step(2)==TrainingStateEvent::RecordingRestarted);
    CC_CHECK_EQ(mem.recordingRestarts,restarts+3);
#ifdef _WIN32
    TestUi();
#endif
    return cccaster::test::Summarize("controller mapping and training state");
}
