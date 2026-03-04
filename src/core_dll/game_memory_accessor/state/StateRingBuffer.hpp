#pragma once
// ============================================================================
// StateRingBuffer — ロールバック用GameStateリングバッファ
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cfenv>
#include <memory>
#include <iostream>
#include "core_dll/game_memory_accessor/dump/MemDumper.hpp"

namespace cccaster::sync {

class StateRingBuffer {
public:
    static constexpr int MAX_STATES = 15; // ロールバック深度上限

    // MemDumperのtotalSizeに基づいてプールを確保
    void Allocate(size_t stateSize) {
        _stateSize = stateSize;
        _pool.reset(new char[MAX_STATES * stateSize]);
        memset(_pool.get(), 0, MAX_STATES * stateSize);
        
        for (int i = 0; i < MAX_STATES; ++i) {
            _valid[i] = false;
            _frameIds[i] = 0;
        }
        
        std::cout << "[StateRingBuffer] Allocated: " << MAX_STATES 
                  << " slots x " << stateSize << "B = " 
                  << (MAX_STATES * stateSize / 1024) << "KB\n";
    }

    void Deallocate() {
        _pool.reset();
        _stateSize = 0;
    }

    // SaveState: メモリダンプをリングバッファに保存 + fenv_t保存
    void Save(uint32_t frame, const MemDumper& dumper) {
        int slot = frame % MAX_STATES;
        char* dest = _pool.get() + (slot * _stateSize);
        dumper.SaveState(dest);
        fegetenv(&_fpEnv[slot]);
        _frameIds[slot] = frame;
        _valid[slot] = true;
    }

    // LoadState: リングバッファからメモリダンプを復元 + fenv_t復元
    bool Load(uint32_t frame, const MemDumper& dumper) const {
        int slot = frame % MAX_STATES;
        if (!_valid[slot] || _frameIds[slot] != frame) {
            std::cerr << "[StateRingBuffer] Load failed: frame=" << frame 
                      << " slot=" << slot << " valid=" << _valid[slot]
                      << " stored=" << _frameIds[slot] << "\n";
            return false;
        }
        
        const char* src = _pool.get() + (slot * _stateSize);
        dumper.LoadState(src);
        fesetenv(&_fpEnv[slot]);
        return true;
    }

    // 指定フレームの状態が存在するか
    bool HasState(uint32_t frame) const {
        int slot = frame % MAX_STATES;
        return _valid[slot] && _frameIds[slot] == frame;
    }

    // 書き込み用バッファのポインタを取得（SaveState省略時に直接書き込む用）
    char* GetWriteBuffer(uint32_t frame) {
        int slot = frame % MAX_STATES;
        return _pool.get() + (slot * _stateSize);
    }

    size_t GetStateSize() const { return _stateSize; }

private:
    std::unique_ptr<char[]> _pool;
    size_t _stateSize = 0;
    bool _valid[MAX_STATES] = {};
    uint32_t _frameIds[MAX_STATES] = {};
    std::fenv_t _fpEnv[MAX_STATES] = {};
};

} // namespace cccaster::sync
