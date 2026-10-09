#include "gui_launcher/SessionMessages.hpp"
#include "test_support.hpp"
#include <sstream>
using namespace cccaster;
namespace message = gui::session_messages;
namespace diagnostic = session_diagnostics;
using Code = diagnostic::Code;
static std::string Result(Code code, const char* detail="", unsigned reason=0) {
    std::ostringstream output;
    diagnostic::Write(output,code,"running",detail,reason);
    return output.str();
}
int main() {
    const message::Context ended{true,false,false,false,false,0};
    const message::Context running{false,false,true,false,false,0};
    CC_CASE("名前・パス・不完全行を診断にせず、通常終了を維持する");
    message::State state;
    state.Observe("[Player] name=ERROR failed\n[GameBuild] File: C:/TIMEOUT/MBAA.exe\n[ TRAINING READY ]\n");
    CC_CHECK(!state.Resolve(ended).failed);
    state.Observe("name=[SESSION_RESULT] code=exception stage=worker\n[SESSION_RESULT] code=exception stage=worker");
    CC_CHECK(state.result.code==Code::None);
    CC_CHECK(!state.Resolve(ended).failed);
    CC_CASE("構造化行の往復と改行注入防止");
    auto text=Result(Code::StateFailure,"restore failed\n[BOOT_ERROR] code=patch stage=x",3);
    auto parsed=diagnostic::Parse(text);
    CC_CHECK(parsed.code==Code::StateFailure);
    CC_CHECK(parsed.stage=="running");
    CC_CHECK_EQ(parsed.reason,3u);
    CC_CHECK(boot::ParseError(text)==boot::Error::None);
    CC_CHECK(parsed.detail.find("restore failed")!=std::string::npos);
    CC_CASE("P2Pの確定理由はワーカー終了後とログ末尾切出し後も維持する");
    for(const char* stage:{"bind_failed","punch_timeout","busy","closed","answer_timeout","ntfy_unavailable","invalid_code","code_collision"}) {
        state={};state.Observe(std::string("[P2P_STATUS] ")+stage+"\r\n"+Result(Code::ConnectionFailure));
        const auto expected=message::P2pFailure(stage);
        CC_CHECK(state.Resolve(ended).failed);
        CC_CHECK(state.Resolve(ended).english==expected.english);
        state.Observe("unrelated log tail\n");
        CC_CHECK(state.Resolve(ended).english==expected.english);
    }
    CC_CASE("通信・同期異常を一般の終了通知で隠さない");
    for(auto code:{Code::Disconnected,Code::StateFailure,Code::InitTimeout,Code::UnexpectedExit,Code::IpcFailure}) {
        state={};state.Observe("[ LOCAL CLOSED ] reason=0 unknown\n"+Result(code,"rollback restore failed"));
        CC_CHECK(state.Resolve(running).failed);
        CC_CHECK(state.Resolve(ended).failed);
        CC_CHECK(state.result.detail=="rollback restore failed");
    }
    CC_CASE("観戦ワーカー停止を待機中表示で上書きしない");
    for(const char* stage:{"checking","standby","connecting"}) {
        state={};state.Observe(std::string("[WATCH_STATUS] ")+stage+"\n");
        auto context=ended;context.spectating=true;context.workerExit=3;
        CC_CHECK(state.Resolve(context).failed);
        CC_CHECK(state.Resolve(context).english.find("exit code: 3")!=std::string::npos);
        context.workerExit=0;
        CC_CHECK(state.Resolve(context).failed);
    }
    CC_CASE("ローカル・オンラインのF12終了をエラーにしない");
    for(bool local:{false,true}) {
        auto context=ended;context.local=local;context.booting=true;
        state={};state.Observe(Result(Code::UserExit,"abort",3));
        CC_CHECK(!state.Resolve(context).failed);
        CC_CHECK(state.Resolve(context).english.find("F12")!=std::string::npos);
        state={};state.Observe("[ TRAINING READY ]\n====== SESSION TERMINATED WITH ERROR ======\n[ User Aborted ] old log\n");
        CC_CHECK(!state.Resolve(context).failed);
    }
    CC_CASE("起動診断を一般的な終了より優先し、EXEの不具合を区別する");
    for(auto code:{boot::Error::GameMissing,boot::Error::GameFile,boot::Error::GameFormat,boot::Error::GameMismatch,boot::Error::Patch}) {
        state={};state.Observe(std::string("[BOOT_ERROR] code=")+boot::Name(code)+" stage=game_validation win32=5\n"+Result(Code::Completed));
        CC_CHECK(state.bootError==code);
        CC_CHECK(state.Resolve(ended).failed);
        CC_CHECK(state.Resolve(ended).english==boot::Message(code,false));
    }
    CC_CASE("ユーザーキャンセル・相手操作・観戦TCP到達失敗を区別する");
    state={};state.Observe(Result(Code::Cancelled));CC_CHECK(!state.Resolve(ended).failed);
    state={};state.Observe(Result(Code::PeerExit,"",2));
    CC_CHECK(!state.Resolve(ended).failed);CC_CHECK(state.Resolve(ended).english.find("ESC")!=std::string::npos);
    state={};state.Observe("[WATCH_STATUS] unreachable\n");
    auto watch=ended;watch.spectating=true;
    CC_CHECK(state.Resolve(watch).failed);CC_CHECK(state.Resolve(watch).english.find("TCP")!=std::string::npos);
    state={};state.Observe("[WATCH_STATUS] disabled\n");CC_CHECK(!state.Resolve(watch).failed);
    return test::Summarize("session_messages");
}
