#pragma once

#include <cstdint>
#include <iostream>

namespace cccaster {
namespace sync {

    // Netplayの状態遷移を管理する列挙型
    enum class NetplayState : uint8_t {
        Initial = 0,         // The game starting phase (FastBoot skipping)
        CharaSelect = 1,     // Character select (Synchronized)
        Loading = 2,         // Loading screen (after character select)
        CharaIntro = 3,      // Character Introductions
        Skippable = 4,       // Skippable states (round transitions, post-game, pre-retry)
        InGame = 5,          // Main in-game versus state
        RetryMenu = 6,       // Post-game retry menu
        ReplayMenu = 7       // Replay select menu (offline)
    };

    // フェーズ遷移番号(index)とそのフェーズ内での経過フレーム(frame)を示す複合ID
    // 異なる画面ステートの古いパケットが新しいステートのパケットに混ざるのを防ぐために使用
    union IndexedFrame {
        struct {
            uint32_t frame;
            uint32_t index;
        } parts;
        uint64_t value;

        bool operator==(const IndexedFrame& other) const {
            return value == other.value;
        }
        bool operator!=(const IndexedFrame& other) const {
            return value != other.value;
        }
        bool operator<(const IndexedFrame& other) const {
            return value < other.value;
        }
        bool operator>(const IndexedFrame& other) const {
            return value > other.value;
        }
    };

    const IndexedFrame MaxIndexedFrame = { { UINT32_MAX, UINT32_MAX } };

    inline std::ostream& operator<<(std::ostream& os, const IndexedFrame& indexedFrame) {
        return (os << indexedFrame.parts.index << ':' << indexedFrame.parts.frame);
    }

} // namespace sync
} // namespace cccaster
