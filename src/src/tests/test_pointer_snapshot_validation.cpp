#include "core_dll/rollback/PointerSnapshot.hpp"
#include "test_support.hpp"
#include <array>

using namespace cccaster::sync;
static uintptr_t rejectedAddress;
static bool RejectOne(uintptr_t address, size_t, bool) { return address != rejectedAddress; }

int main() {
    CC_CASE("後方rootの不正領域は先行rootにも書き戻さない");
    std::array<uint32_t, 2> roots{11, 22};
    SnapshotNode pair[] = {{-1, reinterpret_cast<uintptr_t>(&roots[0]), 0, 4},
                           {-1, reinterpret_cast<uintptr_t>(&roots[1]), 0, 4}};
    PointerSnapshot snapshot;
    CC_CHECK(snapshot.Configure(pair, true, RejectOne));
    std::vector<char> blob(snapshot.Size());
    CC_CHECK(snapshot.Save(blob));
    roots = {33, 44};
    rejectedAddress = reinterpret_cast<uintptr_t>(&roots[1]);
    CC_CHECK(!snapshot.ValidateLoad(blob));
    CC_CHECK(!snapshot.Load(blob));
    CC_CHECK_EQ(roots[0], 33);
    CC_CHECK_EQ(roots[1], 44);
    rejectedAddress = 0;
    CC_CHECK(snapshot.Load(blob));
    CC_CHECK_EQ(roots[0], 11);
    CC_CHECK_EQ(roots[1], 22);

    CC_CASE("32bit子ポインターのoffsetと範囲のoverflowを拒否");
    uint32_t parent = 0;
    SnapshotNode tree[] = {{-1, reinterpret_cast<uintptr_t>(&parent), 0, 4}, {0, 0, 8, 4}};
    CC_CHECK(snapshot.Configure(tree));
    blob.resize(snapshot.Size());
    uint32_t pointer = 0xfffffffc;
    std::memcpy(blob.data(), &pointer, 4);
    CC_CHECK(!snapshot.ValidateLoad(blob));
    CC_CHECK(!snapshot.Load(blob));
    CC_CHECK_EQ(parent, 0);
    tree[1].offset = 0;
    tree[1].size = 8;
    CC_CHECK(snapshot.Configure(tree));
    blob.resize(snapshot.Size());
    CC_CHECK(!snapshot.Load(blob));
    CC_CHECK_EQ(parent, 0);

    CC_CASE("null子の子は保存blob内に非zero値があってもskip");
    SnapshotNode nullTree[] = {{-1, reinterpret_cast<uintptr_t>(&parent), 0, 4},
                               {0, 0, 0, 4}, {1, 0, 0, 4}};
    CC_CHECK(snapshot.Configure(nullTree, true, RejectOne));
    blob.assign(snapshot.Size(), char(0xff));
    std::memset(blob.data(), 0, 4);
    parent = 123;
    CC_CHECK(snapshot.ValidateLoad(blob));
    CC_CHECK(snapshot.Load(blob));
    CC_CHECK_EQ(parent, 0);

    if (sizeof(uintptr_t) == 4) {
        CC_CASE("現在の親が不正でも保存時の親から全階層を解決");
        uint32_t leaf = 77, other = 88;
        uint32_t middle = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&leaf));
        parent = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&middle));
        CC_CHECK(snapshot.Configure(nullTree, true, RejectOne));
        CC_CHECK(snapshot.Save(blob));
        parent = 1;
        middle = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&other));
        leaf = 0;
        CC_CHECK(snapshot.ValidateLoad(blob));
        CC_CHECK_EQ(parent, 1);
        CC_CHECK(snapshot.Load(blob));
        CC_CHECK_EQ(leaf, 77);
        CC_CHECK_EQ(other, 88);

        CC_CASE("解放先相当の子を拒否して親の書込みを防ぐ");
        parent = 1;
        rejectedAddress = reinterpret_cast<uintptr_t>(&leaf);
        CC_CHECK(!snapshot.Load(blob));
        CC_CHECK_EQ(parent, 1);
        rejectedAddress = 0;
    }
#ifdef _WIN32
    CC_CASE("VirtualQueryで境界後のread-onlyとguardを確認・前回cacheを破棄");
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const size_t pageSize = info.dwPageSize;
    auto *pages = static_cast<char *>(VirtualAlloc(nullptr, pageSize * 2,
                                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CC_CHECK(pages != nullptr);
    if (pages) {
        std::memset(pages, 17, pageSize * 2);
        SnapshotNode spanning[] = {{-1, reinterpret_cast<uintptr_t>(pages + pageSize - 4), 0, 8}};
        CC_CHECK(snapshot.Configure(spanning, true));
        blob.resize(snapshot.Size());
        CC_CHECK(snapshot.Save(blob));
        CC_CHECK(snapshot.ValidateLoad(blob));
        DWORD previous = 0;
        CC_CHECK(VirtualProtect(pages + pageSize, pageSize, PAGE_READONLY, &previous) != 0);
        pages[pageSize - 4] = 23;
        CC_CHECK(!snapshot.Load(blob));
        CC_CHECK_EQ(pages[pageSize - 4], 23);
        CC_CHECK(snapshot.Save(blob));
        CC_CHECK(VirtualProtect(pages + pageSize, pageSize, PAGE_READWRITE | PAGE_GUARD, &previous) != 0);
        CC_CHECK(!snapshot.Save(blob));
        CC_CHECK(!snapshot.ValidateLoad(blob));
        CC_CHECK(VirtualProtect(pages + pageSize, pageSize, PAGE_READWRITE, &previous) != 0);
        CC_CHECK(snapshot.Load(blob));
        CC_CHECK(VirtualFree(pages, 0, MEM_RELEASE) != 0);
        CC_CHECK(!snapshot.ValidateLoad(blob));
    }
#endif
    return cccaster::test::Summarize("pointer snapshot validation");
}
