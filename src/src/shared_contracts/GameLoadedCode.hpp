#pragma once
#include "GameBuild.hpp"
#include <algorithm>
#include <vector>

namespace cccaster::game_build {
// ファイルRVAを境界検査してファイル位置へ変換する。仮想ゼロ埋め領域は読まない。
inline bool FileOffset(std::span<const uint8_t> file, uint32_t rva, uint32_t bytes,
                       size_t& offset) {
    PeIdentity identity;
    if (!ReadHeaders(file, identity) || !bytes) return false;
    const size_t nt = U32(file, 0x3c), optional = nt + 24;
    const size_t table = optional + U16(file, nt + 20);
    for (size_t i = 0; i < U16(file, nt + 6); ++i) {
        const size_t section = table + i * 40;
        const uint32_t start = U32(file, section + 12), rawSize = U32(file, section + 16);
        if (rva < start || !Fits(rawSize, rva - start, bytes)) continue;
        const uint64_t position = uint64_t(U32(file, section + 20)) + rva - start;
        if (position > file.size() || !Fits(file.size(), size_t(position), bytes)) return false;
        offset = size_t(position);
        return true;
    }
    return false;
}

// 既知EXEの.textをWindowsと同じHIGHLOW再配置後に照合する。
// ASLRの実配置に加え、ランチャーによる入口のEB FEだけを許す。
inline bool ValidateLoadedCode(std::span<const uint8_t> file,
                               std::span<const uint8_t> loadedText,
                               uint32_t actualBase, bool allowEntryLock = true) {
    const auto edition = IdentifyFile(file);
    PeIdentity p;
    if (edition == Edition::Unknown || !ReadHeaders(file, p) ||
        !actualBase || loadedText.size() != p.textSize ||
        uint64_t(actualBase) + p.imageSize > 0x100000000ull) return false;
    std::vector<uint8_t> expected(file.begin() + p.textOffset,
                                  file.begin() + p.textOffset + p.textSize);
    const uint32_t delta = actualBase - p.imageBase;
    const size_t nt = U32(file, 0x3c), optional = nt + 24;
    const size_t optionalSize = U16(file, nt + 20);
    if (optionalSize < 144 || U32(file, optional + 92) < 6) return !delta &&
        std::equal(expected.begin(), expected.end(), loadedText.begin());
    const uint32_t relocRva = U32(file, optional + 136), relocSize = U32(file, optional + 140);
    if (delta && (!relocRva || !relocSize)) return false;
    size_t relocOffset = 0;
    if (relocSize && !FileOffset(file, relocRva, relocSize, relocOffset)) return false;
    const auto reloc = file.subspan(relocOffset, relocSize);
    size_t cursor = 0;
    while (cursor < reloc.size()) {
        if (!Fits(reloc.size(), cursor, 8)) return false;
        const uint32_t page = U32(reloc, cursor), block = U32(reloc, cursor + 4);
        if (block < 8 || block % 2 || !Fits(reloc.size(), cursor, block)) return false;
        for (size_t entry = cursor + 8; entry < cursor + block; entry += 2) {
            const uint16_t item = U16(reloc, entry), type = item >> 12;
            if (!type) continue;
            const uint64_t rva = uint64_t(page) + (item & 0xfff);
            if (type != 3 || rva + 4 > p.imageSize) return false;
            if (rva + 4 <= p.textRva || rva >= uint64_t(p.textRva) + p.textSize) continue;
            if (rva < p.textRva || !Fits(expected.size(), size_t(rva - p.textRva), 4)) return false;
            const size_t at = size_t(rva - p.textRva);
            const uint32_t value = U32(expected, at) + delta;
            for (unsigned b = 0; b < 4; ++b) expected[at + b] = uint8_t(value >> (8 * b));
        }
        cursor += block;
    }
    if (p.entryRva < p.textRva || !Fits(expected.size(), p.entryRva - p.textRva, 2)) return false;
    const size_t entry = p.entryRva - p.textRva;
    if (allowEntryLock && loadedText[entry] == 0xeb && loadedText[entry + 1] == 0xfe) {
        expected[entry] = 0xeb;
        expected[entry + 1] = 0xfe;
    }
    return std::equal(expected.begin(), expected.end(), loadedText.begin());
}
}
