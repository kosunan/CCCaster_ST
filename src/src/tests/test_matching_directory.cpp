#include "test_support.hpp"
#include "p2p/Matching.hpp"
#include <limits>

int main() {
    using namespace cccaster::matching;
    Identity identity, stranger;
    auto record = identity.Sign({{"v",1},{"kind","matching"},{"id",IdentityId(identity)},
        {"code","ABC123"},{"name","Player"},{"comment","Test"},{"spectators",true},
        {"revision",uint64_t(1)},{"action","register"}});
    CC_CASE("公開一覧は平文を含めず毎回異なる認証付き暗号文を投稿する");
    const auto& topic = DirectoryTopic();
    CC_CHECK(topic.size()==32 && topic.find_first_not_of("0123456789abcdef")==std::string::npos);
    const auto sealed = SealDirectory(topic, record);
    CC_CHECK(sealed != SealDirectory(topic, record));
    CC_CHECK(!Json::parse(sealed, nullptr, false).is_object());
    CC_CHECK(sealed.find("Player")==std::string::npos && sealed.find("ABC123")==std::string::npos);
    Json opened;
    CC_CHECK(OpenDirectory(topic, sealed, opened)); CC_CHECK(opened == record);
    CC_CASE("平文・送信先差替え・nonce暗号文タグの改変を公開履歴として受理しない");
    CC_CHECK(!OpenDirectory(topic, record.dump(), opened)); CC_CHECK(opened.is_null());
    CC_CHECK(!OpenDirectory(topic + "0", sealed, opened));
    const auto bytes = cccaster::p2p::Unbase64(sealed);
    for (const size_t offset : {size_t(0), size_t(12), bytes.size()-1}) {
        auto changed = bytes; changed[offset] ^= 1;
        CC_CHECK(!OpenDirectory(topic, cccaster::p2p::Base64(changed), opened));
    }
    CC_CHECK(!OpenDirectory(topic, std::string(4097, 'A'), opened));
    CC_CASE("共通鍵で暗号化できても他人の登録署名を改変できない");
    auto altered = record; altered["name"] = "FORGED";
    CC_CHECK(OpenDirectory(topic, SealDirectory(topic, altered), opened));
    Directory untrusted;
    CC_CHECK(!untrusted.Apply(opened)); CC_CHECK(untrusted.People().empty());
    Directory directory;
    CC_CASE("公開登録と取消は同じ署名鍵に束縛する");
    CC_CHECK(Registration(record)); CC_CHECK(directory.Apply(record));
    CC_CHECK_EQ(directory.People().size(),1u);
    auto forged=record; forged["action"]="remove"; forged["revision"]=uint64_t(2);
    CC_CHECK(!directory.Apply(forged));
    CC_CHECK(!directory.Apply(stranger.Sign(forged)));
    auto removed=identity.Sign(forged);
    CC_CHECK(directory.Apply(removed)); CC_CHECK(directory.People().empty());
    CC_CASE("古い開始通知で取消を巻き戻さない");
    CC_CHECK(!directory.Apply(record)); CC_CHECK(directory.People().empty());
    record["revision"]=uint64_t(3);record=identity.Sign(record);
    CC_CHECK(directory.Apply(record)); CC_CHECK(!directory.Apply(removed));
    CC_CHECK_EQ(directory.People().size(),1u);
    CC_CASE("公開再開でコードを維持し、確認結果をその行だけへ反映する");
    directory.Result(IdentityId(identity),"busy");
    CC_CHECK(directory.People()[0].code=="ABC123");
    CC_CHECK(directory.People()[0].result=="busy");
    CC_CHECK_EQ(PageSize,20u);
    CC_CASE("署名済みでも不正な形式を登録として扱わない");
    record["code"]="bad";CC_CHECK(!Registration(identity.Sign(record)));
    CC_CHECK(!Verified(nlohmann::json::array()));
    CC_CASE("20名を超える登録も保持しページ境界で欠落しない");
    for (unsigned i=0;i<24;++i) {
        Identity person;
        auto item=person.Sign({{"v",1},{"kind","matching"},{"id",IdentityId(person)},
            {"code",cccaster::p2p::NewCode()},{"name",std::to_string(i)},{"comment",""},
            {"spectators",true},{"revision",uint64_t(1)},{"action","register"}});
        CC_CHECK(directory.Apply(item));
    }
    CC_CHECK_EQ(directory.People().size(),25u);
    CC_CHECK_EQ((directory.People().size()+PageSize-1)/PageSize,2u);
    CC_CASE("取消1件で公開から6時間以上の掲載だけを掃除する");
    constexpr int64_t now = 2'000'000'000;
    constexpr int64_t cutoff = now - PublicListingLifetimeSeconds;
    auto listing = [](const Identity& owner, int64_t at) {
        return owner.Sign({{"v",1},{"kind","matching"},{"id",IdentityId(owner)},
            {"code","ABC123"},{"name","Player"},{"comment",""},{"spectators",true},
            {"revision",uint64_t(1)},{"action","register"},{"listed_at",at}});
    };
    Identity old, boundary, fresh, legacy, undated, renewed, cleaner;
    const auto oldRecord = listing(old, cutoff-1);
    const auto boundaryRecord = listing(boundary, cutoff);
    const auto freshRecord = listing(fresh, cutoff+1);
    auto legacyRecord = listing(legacy, cutoff-1);
    legacyRecord.erase("listed_at"); legacyRecord["created"] = cutoff-1;
    legacyRecord = legacy.Sign(legacyRecord);
    auto undatedRecord = listing(undated, now);
    undatedRecord.erase("listed_at"); undatedRecord = undated.Sign(undatedRecord);
    auto renewedRecord = listing(renewed, now);
    renewedRecord["created"] = cutoff-100; renewedRecord = renewed.Sign(renewedRecord);
    const std::vector<Json> records{oldRecord,boundaryRecord,freshRecord,legacyRecord,undatedRecord,renewedRecord};
    auto cleanup = listing(cleaner, now);
    cleanup["action"] = "remove"; cleanup["cleanup_before"] = cutoff;
    cleanup = cleaner.Sign(cleanup);
    Directory aged;
    for(const auto& item:records) CC_CHECK(aged.Apply(item, now));
    CC_CHECK_EQ(aged.People().size(),6u); // 時間経過だけでは掃除しない。
    CC_CHECK(aged.Apply(cleanup, now));
    CC_CHECK_EQ(aged.People().size(),3u);
    CC_CHECK(aged.People()[0].id == IdentityId(fresh));
    CC_CHECK(aged.People()[1].id == IdentityId(undated));
    CC_CHECK(aged.People()[2].id == IdentityId(renewed));
    CC_CHECK(aged.Expired(cutoff)); CC_CHECK(!aged.Expired(cutoff+1)); CC_CHECK(!aged.Expired(0));
    CC_CASE("掲載開始も掃除を同梱し本人の新規掲載と新しい他人の掲載を残す");
    auto startCleanup = cleanup; startCleanup["action"] = "register";
    startCleanup = cleaner.Sign(startCleanup);
    for (bool reversedOrder : {false, true}) {
        Directory starting;
        if (reversedOrder) CC_CHECK(starting.Apply(startCleanup, now));
        for (const auto& item : records) CC_CHECK(starting.Apply(item, now));
        if (!reversedOrder) CC_CHECK(starting.Apply(startCleanup, now));
        CC_CHECK_EQ(starting.People().size(),4u);
        CC_CHECK(starting.Expired(cutoff)); CC_CHECK(!starting.Expired(cutoff+1));
        CC_CHECK(!starting.Apply(startCleanup, now+PublicListingLifetimeSeconds+100));
        CC_CHECK_EQ(starting.People().size(),4u); // 再配送は掃除時刻を進めない。
    }
    CC_CASE("個人登録の終了は公開一覧全体を掃除しない");
    Directory closing;
    CC_CHECK(closing.Apply(oldRecord, now));
    auto closed = cleanup; closed["action"] = "closed";
    CC_CHECK(closing.Apply(cleaner.Sign(closed), now));
    CC_CHECK_EQ(closing.People().size(),1u);
    CC_CASE("掃除後に受信する古い掲載も復活せず同じコードの再掲載は残る");
    Directory reversed;
    CC_CHECK(reversed.Apply(cleanup, now));
    for(const auto& item:records) CC_CHECK(reversed.Apply(item, now));
    CC_CHECK_EQ(reversed.People().size(),3u);
    auto republished = oldRecord;
    republished["revision"] = uint64_t(2); republished["listed_at"] = now;
    CC_CHECK(reversed.Apply(old.Sign(republished), now));
    CC_CHECK_EQ(reversed.People().size(),4u);
    CC_CHECK(reversed.People().back().code == "ABC123");
    CC_CHECK(!reversed.Apply(oldRecord, now));
    CC_CHECK(!reversed.Apply(cleanup, now+PublicListingLifetimeSeconds+100));
    CC_CHECK_EQ(reversed.People().size(),4u); // 古い取消の再配送で掃除時刻を進めない。
    CC_CASE("新しいrevisionの後に届いた取消も他人の掃除境界を反映する");
    Directory outOfOrder;
    auto newerCleaner = listing(cleaner, now);
    newerCleaner["revision"] = uint64_t(2); newerCleaner = cleaner.Sign(newerCleaner);
    CC_CHECK(outOfOrder.Apply(newerCleaner, now));
    CC_CHECK(outOfOrder.Apply(oldRecord, now));
    CC_CHECK(outOfOrder.Apply(cleanup, now));
    CC_CHECK_EQ(outOfOrder.People().size(),1u);
    CC_CHECK(outOfOrder.People()[0].id == IdentityId(cleaner));
    CC_CASE("掃除境界の改変を拒否し未来の境界でも6時間未満は消さない");
    Directory guarded;
    CC_CHECK(guarded.Apply(oldRecord, now)); CC_CHECK(guarded.Apply(freshRecord, now));
    auto futureCleanup = cleanup; futureCleanup["cleanup_before"] = now+PublicListingLifetimeSeconds;
    CC_CHECK(!guarded.Apply(futureCleanup, now));
    CC_CHECK(!guarded.Apply(stranger.Sign(futureCleanup), now));
    CC_CHECK_EQ(guarded.People().size(),2u);
    auto registerCleanup = futureCleanup; registerCleanup["action"] = "register";
    CC_CHECK(!guarded.Apply(registerCleanup, now));
    CC_CHECK(!guarded.Apply(stranger.Sign(registerCleanup), now));
    CC_CHECK_EQ(guarded.People().size(),2u);
    CC_CHECK(guarded.Apply(cleaner.Sign(registerCleanup), now));
    CC_CHECK_EQ(guarded.People().size(),2u); // 古い掲載だけを除外し、本人と新しい掲載を残す。
    futureCleanup["revision"] = uint64_t(2);
    CC_CHECK(guarded.Apply(cleaner.Sign(futureCleanup), now));
    CC_CHECK_EQ(guarded.People().size(),1u);
    CC_CHECK(guarded.People()[0].id == IdentityId(fresh));
    CC_CASE("署名済みでも不正な時刻を掃除や掲載に使わない");
    for(const auto* field:{"created","listed_at","cleanup_before"}) {
        for(const Json invalid:{Json(-1),Json(0),Json(1.5),Json("123"),Json(nullptr),Json(std::numeric_limits<uint64_t>::max())}) {
            auto malformed = cleanup; malformed[field] = invalid;
            CC_CHECK(!Registration(cleaner.Sign(malformed)));
        }
    }
    return cccaster::test::Summarize("matching_directory");
}
