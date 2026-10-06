#include "core_dll/ui/FontAtlasSnapshot.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

void check(bool condition) { if (!condition) std::abort(); }
using namespace cccaster::startup_cache;
using namespace cccaster::hud::font_snapshot;
int main() {
    const auto root = std::filesystem::temp_directory_path() /
        (L"CCCaster_restore_\u691c\u8a3c_" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    Store store(root, 9), otherKind(root, 10);
    Digest key{}; Hash hash; hash.Add("source", 6); check(hash.Finish(key));
    Bytes original{1, 2, 3, 4, 5}, actual{9};
    check(!store.Read(key, actual) && actual == Bytes{9});
    check(store.Write(key, original));
    check(store.Read(key, actual) && actual == original);
    check(!otherKind.Read(key, actual));
    auto changed = key; changed[0] ^= 1; check(!store.Read(changed, actual));
    const auto file = root / (Hex(key) + ".cache");
    // ヘッダー破損、入力キー変更、過大長、ペイロード改変、途中書込みをすべて拒否。
    for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(12), size_t(16), sizeof(Header)}) {
        check(store.Write(key, original));
        std::fstream io(file, std::ios::in | std::ios::out | std::ios::binary);
        io.seekg(offset); char byte = 0; io.read(&byte, 1); byte ^= 0x7f;
        io.seekp(offset); io.write(&byte, 1); io.close();
        actual = {9}; check(!store.Read(key, actual) && actual == Bytes{9});
    }
    check(store.Write(key, original)); std::filesystem::resize_file(file, sizeof(Header) + 2);
    check(!store.Read(key, actual));
    Store unavailable(root / "blocked" / "child", 9);
    { std::ofstream blocked(root / "blocked"); blocked << "file"; }
    check(!unavailable.Write(key, original));

    // 実stbビルダーから作ったatlasと、別atlasに復元した文字配置・描画命令を比較する。
    for (float size : {13.0f, 19.5f, 45.0f}) {
        ImFontAtlas native, restored;
        for (auto* atlas : {&native, &restored}) {
            ImFontConfig config; config.SizePixels = size; atlas->AddFontDefault(&config);
            config.SizePixels = size + 3; atlas->AddFontDefault(&config);
        }
        Digest a{}, b{};
        check(Key(&native, a) && Key(&restored, b) && a == b);
        check(native.Build());
        const auto bytes = Encode(&native); check(!bytes.empty());
        check(Decode(&restored, bytes) && restored.IsBuilt() && Encode(&restored) == bytes);
        for (int i = 0; i < native.Fonts.Size; ++i) {
            auto* x = native.Fonts[i]; auto* y = restored.Fonts[i];
            check(x->IndexLookup.Size == y->IndexLookup.Size && x->IndexAdvanceX.Size == y->IndexAdvanceX.Size);
            check(!std::memcmp(x->IndexLookup.Data, y->IndexLookup.Data, x->IndexLookup.size_in_bytes()));
            check(!std::memcmp(x->IndexAdvanceX.Data, y->IndexAdvanceX.Data, x->IndexAdvanceX.size_in_bytes()));
            const char* sample = "CCCaster Training 123...?\tMissing \xE6\x97\xA5";
            auto sx = x->CalcTextSizeA(size, 1000, 95, sample), sy = y->CalcTextSizeA(size, 1000, 95, sample);
            check(sx.x == sy.x && sx.y == sy.y);
            ImDrawListSharedData shared;
            ImDrawList dx(&shared), dy(&shared);
            for (auto* draw : {&dx, &dy}) { draw->_ResetForNewFrame(); draw->PushTextureID(nullptr); draw->PushClipRectFullScreen(); }
            x->RenderText(&dx, size, ImVec2(5, 7), 0xffffffff, ImVec4(0, 0, 1000, 1000), sample, nullptr, 95);
            y->RenderText(&dy, size, ImVec2(5, 7), 0xffffffff, ImVec4(0, 0, 1000, 1000), sample, nullptr, 95);
            check(dx.VtxBuffer.Size == dy.VtxBuffer.Size && dx.IdxBuffer.Size == dy.IdxBuffer.Size);
            check(!std::memcmp(dx.VtxBuffer.Data, dy.VtxBuffer.Data, dx.VtxBuffer.size_in_bytes()));
            check(!std::memcmp(dx.IdxBuffer.Data, dy.IdxBuffer.Data, dx.IdxBuffer.size_in_bytes()));
        }
        for (int cursor = 0; cursor < ImGuiMouseCursor_COUNT; ++cursor) {
            ImVec2 x[6]{}, y[6]{};
            check(native.GetMouseCursorTexData(cursor, &x[0], &x[1], x + 2, x + 4));
            check(restored.GetMouseCursorTexData(cursor, &y[0], &y[1], y + 2, y + 4));
            check(!std::memcmp(x, y, sizeof(x)));
        }
        // 寸法/NaN/欠損を拒否し、既存の正しいatlasを壊さない。
        auto bad = bytes;
        Atlas header{}; std::memcpy(&header, bad.data(), sizeof(header));
        header.width = UINT32_MAX; std::memcpy(bad.data(), &header, sizeof(header));
        check(!Decode(&restored, bad) && Encode(&restored) == bytes);
        header.width = native.TexWidth; header.white.x = std::numeric_limits<float>::quiet_NaN();
        std::memcpy(bad.data(), &header, sizeof(header)); check(!Decode(&restored, bad));
        check(!Decode(&restored, std::span(bytes).first(bytes.size() - 1)));
        check(Key(&native, a) && Key(&restored, b) && a == b);
        restored.Fonts[0]->Scale = 2; check(Key(&restored, b) && a != b);
        restored.Fonts[0]->Scale = 1;
        restored.ConfigData[0].OversampleH += 1; check(Key(&restored, b) && a != b);
        restored.ConfigData[0].OversampleH -= 1;
        static_cast<uint8_t*>(restored.ConfigData[0].FontData)[0] ^= 1;
        check(Key(&restored, b) && a != b);
    }
    std::filesystem::remove_all(root); // このテストが作った一時ディレクトリのみ。
    std::puts("startup_restore: cache rejection, atlas pixels/metrics/draw data/cursors matched");
}
