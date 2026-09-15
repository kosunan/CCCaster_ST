#pragma once
#include "GameRuntime.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <windows.h>

namespace cccaster::mbaa::steam::replay {
// Steam 2017-01-05。VS iterator debug headerの有無で旧版から4Bずつ縮む。
#pragma pack(push, 1)
struct State { uint8_t bytes[8]; };
struct Container {
    State *states;
    State *end;
    State *capacity;
    int32_t total, total2, index;
    uint32_t unknown;
};
struct Round {
    uint8_t prefix[0x11c];
    Container *inputs, *inputsEnd, *inputsCapacity;
    uint32_t unknown;
    uint32_t *rng, *rngEnd, *rngCapacity;
};
#pragma pack(pop)
static_assert(sizeof(void *) == 4);
static_assert(sizeof(Container) == 0x1c && sizeof(Round) == 0x138);
struct Cursor {
    int32_t total = 0, total2 = 0, index = 0;
    uint32_t endOffset = 0;
    State last{};
};
struct Snapshot {
    uintptr_t round = 0;
    std::array<Cursor, 4> players{};
    uint32_t rngEndOffset = 0;
};
inline bool Readable(const void *p, size_t size) { return p && !IsBadReadPtr(p, size); }
inline bool Current(Round *&round) {
    round = nullptr;
    if (!game_memory::GameRuntime::IsSteam()) return false;
    const auto slot = game_memory::GameRuntime::Preferred(0x7e9bfc, 16);
    if (!slot || !Readable(reinterpret_cast<const void *>(slot), 16)) return false;
    const auto begin = *reinterpret_cast<Round **>(slot);
    const auto end = *reinterpret_cast<Round **>(slot + 4);
    const auto capacity = *reinterpret_cast<Round **>(slot + 8);
    const auto index = *reinterpret_cast<const int32_t *>(slot + 12);
    const auto length = uintptr_t(end) - uintptr_t(begin);
    if ((!begin && end) || length > 0x138 * 1000 || length % sizeof(Round) ||
        uintptr_t(end) > uintptr_t(capacity) || index < 0) return false;
    // 0x499D07 / 0x49D91A は現在のround添字を使用する。Trainingでは
    // 0x4996B4が先に10個を確保するため、vector最後尾は記録先ではない。
    if (uint32_t(index) > length / sizeof(Round)) return false;
    // intro=2では次のroundが未作成。前round末尾を保存しない。
    if (uint32_t(index) == length / sizeof(Round)) return true;
    round = reinterpret_cast<Round *>(uintptr_t(begin) + uint32_t(index) * sizeof(Round));
    return Readable(round, sizeof(Round));
}
inline bool Capture(Snapshot &result) {
    result = {};
    Round *round;
    if (!Current(round)) return false;
    result.round = *reinterpret_cast<const uint32_t *>(game_memory::GameRuntime::Preferred(0x7e9c08)) + 1u;
    if (!round || !round->inputs) return true;
    if (uintptr_t(round->inputsEnd) - uintptr_t(round->inputs) != 4 * sizeof(Container) ||
        !Readable(round->inputs, 4 * sizeof(Container))) return false;
    for (size_t i = 0; i < 4; ++i) {
        const auto &c = round->inputs[i];
        auto &saved = result.players[i];
        const auto length = uintptr_t(c.end) - uintptr_t(c.states);
        if (length > 16000000 || length % 8 || c.index < 0 || c.index > 1000000 ||
            uintptr_t(c.end) > uintptr_t(c.capacity)) return false;
        saved.total = c.total; saved.total2 = c.total2; saved.index = c.index;
        saved.endOffset = static_cast<uint32_t>(length);
        if (length) {
            if (uint32_t(c.index) >= length / 8 || !Readable(c.states, length)) return false;
            saved.last = c.states[c.index];
        }
    }
    const auto rngLength = uintptr_t(round->rngEnd) - uintptr_t(round->rng);
    if (rngLength > 16000000 || rngLength % 4 ||
        uintptr_t(round->rngEnd) > uintptr_t(round->rngCapacity) ||
        (rngLength && !Readable(round->rng, rngLength))) return false;
    result.rngEndOffset = static_cast<uint32_t>(rngLength);
    return true;
}
// ゲームスレッド上でのみ実行。全対象を検証してから書込み、再確保後の現ポインターを使う。
inline bool Restore(const Snapshot &saved) {
    Round *round;
    if (!Current(round)) return false;
    const auto index = *reinterpret_cast<const uint32_t *>(game_memory::GameRuntime::Preferred(0x7e9c08)) + 1u;
    if (index != saved.round) return false;
    if (!round || !round->inputs) {
        if (saved.rngEndOffset) return false;
        for (const auto &s : saved.players)
            if (s.endOffset || s.index || s.total || s.total2) return false;
        return true;
    }
    Snapshot current;
    if (!Capture(current)) return false;
    for (size_t i = 0; i < 4; ++i) {
        const auto &s = saved.players[i];
        const auto &c = current.players[i];
        if (s.endOffset > c.endOffset || s.endOffset % 8 || s.index < 0 ||
            (s.endOffset && uint32_t(s.index) >= s.endOffset / 8) ||
            (c.endOffset && IsBadWritePtr(round->inputs[i].states, c.endOffset))) return false;
    }
    if (saved.rngEndOffset > current.rngEndOffset || saved.rngEndOffset % 4 ||
        (current.rngEndOffset && IsBadWritePtr(round->rng, current.rngEndOffset)) ||
        IsBadWritePtr(round->inputs, 4 * sizeof(Container)) || IsBadWritePtr(round, sizeof(Round))) return false;
    for (size_t i = 0; i < 4; ++i) {
        auto &c = round->inputs[i]; const auto &s = saved.players[i];
        if (current.players[i].endOffset > s.endOffset)
            std::memset(reinterpret_cast<uint8_t *>(c.states) + s.endOffset, 0, current.players[i].endOffset - s.endOffset);
        c.end = reinterpret_cast<State *>(uintptr_t(c.states) + s.endOffset);
        c.total = s.total; c.total2 = s.total2; c.index = s.index;
        if (s.endOffset) c.states[s.index] = s.last;
    }
    if (current.rngEndOffset > saved.rngEndOffset)
        std::memset(reinterpret_cast<uint8_t *>(round->rng) + saved.rngEndOffset, 0, current.rngEndOffset - saved.rngEndOffset);
    round->rngEnd = reinterpret_cast<uint32_t *>(uintptr_t(round->rng) + saved.rngEndOffset);
    return true;
}
}
