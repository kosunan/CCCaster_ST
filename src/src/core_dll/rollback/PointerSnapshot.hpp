#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <limits>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cccaster::sync {
struct SnapshotNode {
    int parent;
    uintptr_t source;
    uintptr_t offset;
    size_t size;
};
// 呼出側がゲームスレッドと資源世代を所有する。検証中の並行解放は許可しない。
class PointerSnapshot {
  public:
    using RangeValidator = bool (*)(uintptr_t address, size_t size, bool write);
    bool Configure(std::span<const SnapshotNode> nodes, bool validateMemory = false,
                   RangeValidator validator = nullptr) {
        validateMemory_ = validateMemory;
        validator_ = validator;
        nodes_.clear();
        offsets_.clear();
        addresses_.clear();
        chains_.clear();
        size_ = 0;
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto &n = nodes[i];
            if (n.parent < -1 || n.parent >= int(i) || !n.size || n.size > 16 * 1024 * 1024 ||
                (n.parent >= 0 && (nodes[n.parent].size < 4 || n.source > nodes[n.parent].size - 4)) ||
                n.size > 16 * 1024 * 1024 - size_) {
                nodes_.clear();
                size_ = 0;
                return false;
            }
            offsets_.push_back(size_);
            size_ += n.size;
            nodes_.push_back(n);
        }
        addresses_.resize(nodes_.size());
        chains_.resize(nodes_.size(), false);
        // 連続した4Bノード3段は、コピーした値がそのまま次のアドレスになる。
        // ゲームの0x67BDE8 + index*0x33Cの保存表にはこの形が1000組ある。
        // アドレス値はキャッシュせず、表の形だけを初期化時に判定する。
        for (size_t i = 0; i + 2 < nodes_.size(); ++i) {
            const auto &second = nodes_[i + 1];
            const auto &third = nodes_[i + 2];
            if (nodes_[i].size == 4 && second.size == 4 && third.size == 4 &&
                second.parent == int(i) && third.parent == int(i + 1) &&
                !second.source && !second.offset && !third.source && !third.offset) {
                chains_[i] = true;
                i += 2;
            }
        }
        return !nodes_.empty();
    }
    size_t Size() const {
        return size_;
    }
    bool Save(std::span<char> bytes) {
        return Copy<false>(bytes.data(), bytes.size());
    }
    bool Load(std::span<char> bytes) {
        return Copy<true>(bytes.data(), bytes.size());
    }
    // 他のカーソルなどを書き戻す前にも呼べる。ゲーム状態への書込みは行わない。
    bool ValidateLoad(std::span<const char> bytes) {
        return Resolve<true>(bytes.data(), bytes.size());
    }

  private:
    // memcpyの固定長指定で非整列アドレスも扱う。復元は親を書いてから子へ進む。
    template <bool Restore> static uint32_t CopyWord(uintptr_t addr, char *bytes) {
        uint32_t value = 0;
        if constexpr (Restore) {
            if (addr) {
                std::memcpy(&value, bytes, 4);
                std::memcpy(reinterpret_cast<void *>(addr), &value, 4);
            }
        } else {
            if (addr)
                std::memcpy(&value, reinterpret_cast<const void *>(addr), 4);
            std::memcpy(bytes, &value, 4);
        }
        return value;
    }
    bool CheckRange(uintptr_t address, size_t length, bool write) {
        if (length - 1 > (std::numeric_limits<uintptr_t>::max)() - address)
            return false;
        if (!validateMemory_)
            return true;
        if (validator_)
            return validator_(address, length, write);
#ifdef _WIN32
        // 範囲が複数VirtualQuery領域に跨る場合も最後まで確認する。
        while (length) {
            size_t cachedRemaining = 0;
            for (auto it = checkedRanges_.rbegin(); it != checkedRanges_.rend(); ++it) {
                if (address >= it->address && address - it->address < it->size) {
                    cachedRemaining = it->size - (address - it->address);
                    break;
                }
            }
            if (cachedRemaining) {
                if (length <= cachedRemaining)
                    return true;
                length -= cachedRemaining;
                address += cachedRemaining;
                continue;
            }
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<const void *>(address), &info, sizeof(info)) ||
                info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
                return false;
            const DWORD protection = info.Protect & 0xff;
            const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
                protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            const bool writable = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            if (!readable || (write && !writable))
                return false;
            const uintptr_t start = reinterpret_cast<uintptr_t>(info.BaseAddress);
            if (start > address || address - start >= info.RegionSize)
                return false;
            checkedRanges_.push_back({start, info.RegionSize});
            const size_t remaining = info.RegionSize - (address - start);
            if (length <= remaining)
                return true;
            length -= remaining;
            address += remaining;
        }
        return true;
#else
        // 他OSで検証を有効にする場合は、そのOSの範囲検証を注入する。
        return false;
#endif
    }
    template <bool Restore> bool Resolve(const char *bytes, size_t length) {
        // 許可情報は同じ事前解決内だけ再利用し、前フレームから持ち越さない。
        checkedRanges_.clear();
        if (nodes_.empty() || !bytes || length != size_)
            return false;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const auto &n = nodes_[i];
            uintptr_t addr = n.source;
            if (n.parent >= 0) {
                addr = addresses_[n.parent];
                if (addr) {
                    uint32_t ptr = 0;
                    // 復元先は現在の親値ではなく保存時の親値から決定する。
                    if constexpr (Restore)
                        std::memcpy(&ptr, bytes + offsets_[n.parent] + n.source, 4);
                    else
                        std::memcpy(&ptr, reinterpret_cast<const void *>(addr + n.source), 4);
                    if (ptr && (n.offset > UINT32_MAX - ptr ||
                                n.size - 1 > UINT32_MAX - ptr - n.offset))
                        return false;
                    addr = ptr ? uintptr_t(ptr) + n.offset : 0;
                }
            }
            addresses_[i] = addr;
            if (addr && !CheckRange(addr, n.size, Restore))
                return false;
        }
        return true;
    }
    template <bool Restore> bool Copy(char *bytes, size_t length) {
        // 復元では全ノードを先に検証し、一部だけ復元された状態を避ける。
        if (!Resolve<Restore>(bytes, length))
            return false;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const auto &n = nodes_[i];
            const uintptr_t addr = addresses_[i];
            // Steamでは全書込み先を先に検証し、その解決結果で3段を一括コピーする。
            if (chains_[i]) {
                CopyWord<Restore>(addresses_[i], bytes + offsets_[i]);
                CopyWord<Restore>(addresses_[i + 1], bytes + offsets_[i + 1]);
                CopyWord<Restore>(addresses_[i + 2], bytes + offsets_[i + 2]);
                i += 2;
                continue;
            }
            if (addr) {
                // 現行表の大半を占める4バイト項目は、可変長memcpy呼出しを避ける。
                if (n.size == 4) {
                    if constexpr (Restore)
                        std::memcpy(reinterpret_cast<void *>(addr), bytes + offsets_[i], 4);
                    else
                        std::memcpy(bytes + offsets_[i], reinterpret_cast<void *>(addr), 4);
                } else {
                    if constexpr (Restore)
                        std::memcpy(reinterpret_cast<void *>(addr), bytes + offsets_[i], n.size);
                    else
                        std::memcpy(bytes + offsets_[i], reinterpret_cast<void *>(addr), n.size);
                }
            } else if constexpr (!Restore) {
                if (n.size == 4)
                    std::memset(bytes + offsets_[i], 0, 4);
                else
                    std::memset(bytes + offsets_[i], 0, n.size);
            }
        }
        return true;
    }
    std::vector<SnapshotNode> nodes_;
    std::vector<size_t> offsets_;
    std::vector<uintptr_t> addresses_;
    std::vector<uint8_t> chains_;
    size_t size_ = 0;
    bool validateMemory_ = false;
    RangeValidator validator_ = nullptr;
    struct CheckedRange { uintptr_t address; size_t size; };
    std::vector<CheckedRange> checkedRanges_;
};
} // namespace cccaster::sync
