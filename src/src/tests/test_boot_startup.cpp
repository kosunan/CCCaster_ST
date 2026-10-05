#include "launcher/GameLauncher.hpp"
#include "shared_contracts/IpcData.hpp"
#include "test_support.hpp"
#include <filesystem>
#include <string>
// 手動実ゲーム検証用。ゲームを開始せず、入口停止中のIPC失敗を検査する。
// --preflight は隣接DLLの不一致・欠落・不正形式の検証にも用いる。
int wmain(int argc,wchar_t**argv) {
    if(argc<2) return 2;
    using namespace cccaster;
    main_app::GameLauncher launcher;
    CC_CASE("起動失敗を診断し、この呼出しで作った子だけを終了する");
    CC_CHECK(!launcher.BootAndMonitor(std::filesystem::path(argv[1])));
    const auto &status=launcher.StartupStatus();
    if(argc==2) {
        CC_CHECK(status.error==boot::Error::Ipc);
        CC_CHECK(status.stage==int32_t(boot::Stage::Ipc));
        CC_CHECK(launcher.GetProcessHandle()!=nullptr);
        if(launcher.GetProcessHandle()) CC_CHECK(WaitForSingleObject(launcher.GetProcessHandle(),0)==WAIT_OBJECT_0);
    } else {
        const std::wstring mode=argv[2];
        const auto expected = mode==L"game" ? boot::Error::GameMismatch : mode==L"missing" ? boot::Error::DllFile : mode==L"mismatch" ? boot::Error::BuildMismatch : boot::Error::DllFormat;
        CC_CHECK(status.error==expected);
        CC_CHECK(launcher.GetProcessHandle()==nullptr);
    }
    return test::Summarize("boot_startup");
}
