#include "test_support.hpp"
#include "gui_launcher/GuiProtocol.hpp"
#include "gui_launcher/MatchingPresentation.hpp"
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
    CC_CASE("募集の経過時間は時分に切捨て、未来と不明の時刻を区別する");
    CC_CHECK(PublicListingAge(100, 159) == "0:00");
    CC_CHECK(PublicListingAge(100, 160) == "0:01");
    CC_CHECK(PublicListingAge(100, 3699) == "0:59");
    CC_CHECK(PublicListingAge(100, 3700) == "1:00");
    CC_CHECK(PublicListingAge(100, 5200) == "1:25");
    CC_CHECK(PublicListingAge(100, 360100) == "100:00");
    CC_CHECK(PublicListingAge(200, 100) == "0:00");
    CC_CHECK(PublicListingAge(0, 100) == "--:--");
    CC_CASE("公開一覧は新着順、同時刻はID順、時刻不明は末尾に表示する");
    Json people = {{{"id","missing"}}, {{"id","old"},{"listedAt",100}},
                   {{"id","b"},{"listedAt",200}}, {{"id","a"},{"listedAt",200}}};
    SortPublicPlayers(people);
    CC_CHECK(people[0]["id"] == "a" && people[1]["id"] == "b");
    CC_CHECK(people[2]["id"] == "old" && people[3]["id"] == "missing");
    people[2]["listedAt"] = 300;
    SortPublicPlayers(people);
    CC_CHECK(people[0]["id"] == "old");
    return cccaster::test::Summarize("gui_protocol");
}
