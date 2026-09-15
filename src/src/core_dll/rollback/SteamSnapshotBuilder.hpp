#pragma once
#include "SteamSnapshotLayout.hpp"
#include "SteamRoundCallFragments.hpp"
#include "SteamDisplayFragments.hpp"
#include "SteamMiscFragments.hpp"
#include "SteamSnapshotPointers.hpp"
#include "shared_contracts/GameImageAddress.hpp"
#include <algorithm>

namespace cccaster::sync {
// 各領域を独立にRVA解決する。旧版の隣接順・一括差分には依存しない。
inline bool BuildSteamSnapshotNodes(const game_build::LoadedImage &image,
                                    std::vector<SnapshotNode> &result) {
    result.clear();
    if (!SteamSnapshotLayoutComplete || image.size != 0xf3e000) return false;
    std::vector<SnapshotNode> nodes;
    int objectRoot = -1;
    const auto append = [&](const auto &fragments) {
        for (const auto &f : fragments) {
            const auto address = image.Resolve(f.steamRva, f.size);
            if (!address) return false;
            for (const auto &n : nodes)
                if (address < n.source + n.size && n.source < address + f.size) return false;
            if (f.steamRva == SteamSnapshotObjectPoolRva && f.size == SteamSnapshotObjectPoolBytes)
                objectRoot = static_cast<int>(nodes.size());
            nodes.push_back({-1, address, 0, f.size});
        }
        return true;
    };
    if (!append(SteamSnapshotFragments) || !append(SteamRoundCallAdditionalFragments) ||
        !append(SteamDisplayFragments) || !append(SteamMiscFragments) || !append(SteamIntroFragments) ||
        !steam_pointers::AppendVerifiedNodes(nodes, objectRoot))
        return false;
    result.swap(nodes);
    return true;
}
}
