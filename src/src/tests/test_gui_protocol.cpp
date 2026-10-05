#include "test_support.hpp"
#include "gui_launcher/GuiProtocol.hpp"
using namespace cccaster::gui;
template<class F> bool Rejected(F fn) { try {fn();return false;}catch(const std::exception&){return true;} }
int main() {
    CC_CASE("GUIからの境界値・型・制御文字を検証する");
    CC_CHECK_EQ(Integer(Json{{"port",65535}},"port",1,65535),65535);
    for(auto v : {Json(0),Json(65536),Json(-1),Json(1.5),Json("7500"),Json(true),Json(UINT64_MAX)})
        CC_CHECK(Rejected([&]{Integer(Json{{"port",v}},"port",1,65535);}));
    CC_CHECK(Boolean(Json{{"on",true}},"on"));
    CC_CHECK(Rejected([]{Boolean(Json{{"on",1}},"on");}));
    CC_CHECK(Rejected([]{String(Json{{"code",std::string("abc\0def",7)}},"code",10);}));
    CC_CHECK(Rejected([]{String(Json{{"code","1234567"}},"code",6);}));
    CC_CASE("コード以外の文字を起動引数へ渡さない");
    CC_CHECK(CodeAlphabet("abc123-DEF"));
    CC_CHECK(!CodeAlphabet("abc\" --worker"));
    CC_CHECK(!CodeAlphabet("abc\n123"));
    return cccaster::test::Summarize("gui_protocol");
}
