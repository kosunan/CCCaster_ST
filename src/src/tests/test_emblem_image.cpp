#include "gui_launcher/EmblemImage.hpp"
#include "gui_launcher/EmblemCatalog.hpp"
#include <cstdio>
#include <set>

int main(int argc, char** argv) {
    using namespace cccaster::emblem;
    const auto root = std::filesystem::temp_directory_path() / ("cccaster-emblem-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto path = root / L"国旗.BMP";
    int failures = 0;
    const auto check = [&](bool yes, const char* label) { if (!yes) { ++failures; std::fprintf(stderr, "FAIL %s\n", label); } };
    Image source;
    for (unsigned y = 0; y < Height; ++y) for (unsigned x = 0; x < Width; ++x) {
        const auto i = (y * Width + x) * 4;
        source.pixels[i] = uint8_t(x * 10); source.pixels[i+1] = uint8_t(y * 20);
        source.pixels[i+2] = uint8_t(x + y); source.pixels[i+3] = 255;
    }
    source.id = Hash(source.pixels);
    const auto data = EncodeBitmap(source);
    const auto write = [&](const auto& bytes) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    Image image;
    check(Save(path, source) && Import(path, image) && image.pixels == source.pixels && image.id == source.id,
          "unicode BMP path and uppercase extension, exact pixels");
    check(std::filesystem::file_size(path) == 918 && BmpUint(data, 28, 2) == 24, "saved BMP is 24-bit 288 pixels");
    auto topDown = data;
    for (unsigned i = 0; i < 4; ++i) topDown[22+i] = uint8_t(uint32_t(-int(Height)) >> (i*8));
    for (unsigned y = 0; y < Height; ++y)
        std::memcpy(topDown.data()+54+y*Width*3, data.data()+54+(Height-1-y)*Width*3, Width*3);
    write(topDown);
    check(Import(path, image) && image.pixels == source.pixels, "top-down BMP orientation");
    for (unsigned header : {108u, 124u}) {
        std::vector<uint8_t> extended(14 + header + BitmapPixels);
        std::copy_n(data.begin(), 54, extended.begin());
        const auto put = [&](unsigned at, uint32_t value) { for (unsigned i=0;i<4;++i) extended[at+i]=uint8_t(value>>(i*8)); };
        put(2, extended.size()); put(10, 14+header); put(14, header);
        std::copy(data.begin()+54, data.end(), extended.begin()+14+header);
        write(extended);
        check(Import(path, image) && image.pixels == source.pixels, "V4/V5 24-bit BMP accepted");
    }
    const auto reject = [&](const auto& bytes, const char* label) {
        write(bytes); check(!Import(path, image) && image.pixels == source.pixels && image.id == source.id, label);
    };
    for (auto [offset,value] : std::initializer_list<std::pair<unsigned,uint32_t>>{
             {18,23},{18,25},{22,11},{22,13},{22,0},{18,0xffffffffu},
             {28,1},{28,8},{28,16},{28,32},{26,2},{30,1},{30,3},
             {10,0},{10,0xffffffffu},{14,12},{14,0xffffffffu},{34,863},{2,917}}) {
        auto bad=data;
        const auto count = offset == 26 || offset == 28 ? 2u : 4u;
        for (unsigned i=0;i<count;++i) bad[offset+i]=uint8_t(value>>(i*8));
        reject(bad, "invalid dimension, bit depth, compression or header rejected without changes");
    }
    auto renamedPng = data; renamedPng[0]=0x89; renamedPng[1]='P'; reject(renamedPng, "renamed PNG rejected");
    auto renamedJpg = data; renamedJpg[0]=0xff; renamedJpg[1]=0xd8; reject(renamedJpg, "renamed JPEG rejected");
    for (size_t n : {size_t(0),size_t(20),size_t(53),data.size()-1})
        reject(std::vector<uint8_t>(data.begin(),data.begin()+n), "truncated BMP rejected");
    write(data);
    const auto png = root / "wrong.png";
    std::filesystem::copy_file(path, png);
    check(!Import(png, image), "BMP disguised with wrong extension rejected");
    check(!Import(root / "missing.bmp", image) && image.pixels == source.pixels, "missing file preserves image");
    check(!Save(root / "missing" / "output.bmp", source), "save failure reported");
    std::set<uint32_t> ids;
    check(argc == 2, "preset directory provided");
    if (argc == 2) for (const auto& preset : Presets) {
        Image flag;
        const auto file = std::filesystem::path(argv[1]) / (std::string(preset.code)+".bmp");
        check(Import(file,flag) && Valid(flag) && flag.id && std::filesystem::file_size(file)==918, preset.code);
        check(ids.insert(flag.id).second, "preset pixels distinct");
        check(FindPreset(preset.code) == &preset, "preset catalog lookup");
        for (unsigned i=3;i<Bytes;i+=4) check(flag.pixels[i]==255,"preset opaque");
    }
    check(ids.size()==24 && !FindPreset("zz") && !FindPreset("../jp"), "24 allowlisted presets");
    std::filesystem::remove(path); std::filesystem::remove(png); std::filesystem::remove(root);
    return failures ? 1 : 0;
}
