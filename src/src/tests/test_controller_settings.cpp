#include "gui_launcher/ControllerSettings.hpp"
#include "gui_launcher/AppContext.hpp"
#include "cli_launcher/ConfigManager.hpp"
#include "shared_contracts/NativePath.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace cccaster;
using namespace cccaster::gui;
void Require(bool value,const char* what) {if(!value)throw std::runtime_error(what);}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("cccaster-controller-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root);exePath=root/"CCCaster_Steam_GUI.exe";
    guiWindow=CreateWindowExW(0,L"STATIC",L"controller test",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    int result=0;
    try {
        {std::ofstream main(root/"cccaster_steam.ini");main<<"[Settings]\nP1Device=Keyboard\nP2Device=\n[Keep]\nValue=unchanged\n";}
        {std::ofstream profile(root/"Keyboard.ini");profile<<"[Mapping]\nA=S\nB=D\nUp_Alt=UpArrow\nTrainingSave=Q\n[Keep]\nValue=profile\n";}
        ControllerSettings settings;
        auto command=[&](const Json& c,bool locked=false){settings.Command(c,locked);};
        command({{"action","open"}});
        Require(settings.State(false)["selected"]=="keyboard","keyboard reload");
        command({{"action","capture"},{"row",4}});
        Require(settings.KeyMessage(WM_KEYDOWN,'D',0),"capture consumes key");
        settings.KeyMessage(WM_KEYUP,'D',0);
        auto state=settings.State(false);
        Require(state["bindings"][4]=="D"&&state["bindings"][5]=="--","duplicate moved");
        main_app::Config profile;profile.Load(PathUtf8(root/"Keyboard.ini"));
        Require(profile.GetString("Mapping","Up_Alt")=="UpArrow"&&profile.GetString("Mapping","TrainingSave")=="Q"&&profile.GetString("Keep","Value")=="profile","unrelated settings preserved");
        command({{"action","player"},{"player",1}});command({{"action","device"},{"device","keyboard"}});
        Require(settings.State(false)["selected"]=="","duplicate device rejected");
        command({{"action","player"},{"player",0}});
        command({{"action","capture"},{"row",4}},true);
        Require(settings.State(false)["capture"]==-1,"busy rejects capture");
        command({{"action","capture"},{"row",4}});settings.KeyMessage(WM_KEYDOWN,VK_ESCAPE,0);settings.KeyMessage(WM_KEYUP,VK_ESCAPE,0);
        Require(settings.State(false)["bindings"][4]=="D"&&settings.State(false)["capture"]==-1,"escape preserves binding");
        command({{"action","capture"},{"row",4}});settings.KeyMessage(WM_KEYDOWN,VK_F4,0);settings.KeyMessage(WM_KEYUP,VK_F4,0);
        Require(settings.State(false)["capture"]==4,"reserved key not captured");command({{"action","cancel"}});
        std::filesystem::create_directory(root/"Keyboard.ini.tmp");
        command({{"action","clear"},{"row",4}});
        Require(settings.State(false)["bindings"][4]=="D","failed save preserves mapping");
        std::filesystem::remove(root/"Keyboard.ini.tmp");
        command({{"action","clear"},{"row",4}});command({{"action","close"}});command({{"action","open"}});
        Require(settings.State(false)["bindings"][4]=="--","clear persists across reopen");
        command({{"action","device"},{"device",""}});command({{"action","player"},{"player",1}});command({{"action","device"},{"device","keyboard"}});
        main_app::Config main;main.Load(PathUtf8(root/"cccaster_steam.ini"));
        Require(main.GetString("Settings","P1Device").empty()&&main.GetString("Settings","P2Device")=="Keyboard"&&main.GetString("Keep","Value")=="unchanged","allocation and unrelated main settings");
        command({{"action","capture"},{"row",5}});command({{"action","close"}});
        Require(settings.State(false)["capture"]==-1,"leaving cancels capture");
        std::cout<<"controller settings: persistence, conflicts, cancellation, busy lock, save failure OK\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    DestroyWindow(guiWindow);guiWindow=nullptr;
    std::filesystem::remove_all(root);return result;
}
