#include "core_dll/game_memory_accessor/state/StateBuffer.hpp"

namespace cccaster::sync {

StateBuffer::StateBuffer(size_t bufferSize) 
    : _capacity(bufferSize), _oldestFrame(0), _confirmedFrame(0) {
    _buffer.resize(_capacity);
}

size_t StateBuffer::GetIndex(uint32_t frameId) const {
    return frameId % _capacity;
}

void StateBuffer::SaveLocalInput(uint32_t frameId, uint16_t input) {
    size_t idx = GetIndex(frameId);
    _buffer[idx].frameId = frameId;
    _buffer[idx].localInput = input;
}

void StateBuffer::SaveRemoteInput(uint32_t frameId, uint16_t input) {
    size_t idx = GetIndex(frameId);
    _buffer[idx].frameId = frameId;
    _buffer[idx].remoteInput = input;
}

void StateBuffer::SaveState(uint32_t frameId, const std::vector<uint8_t>& memoryData) {
    size_t idx = GetIndex(frameId);
    _buffer[idx].frameId = frameId;
    _buffer[idx].state.frameId = frameId;
    _buffer[idx].state.memoryDump = memoryData; // パフォーマンス的にここは要改善の余地あり(ポインタやメモリプール等)
    _buffer[idx].state.isValid = true;
}

std::optional<uint16_t> StateBuffer::GetLocalInput(uint32_t frameId) const {
    size_t idx = GetIndex(frameId);
    if (_buffer[idx].frameId == frameId) {
        return _buffer[idx].localInput;
    }
    return std::nullopt;
}

std::optional<uint16_t> StateBuffer::GetRemoteInput(uint32_t frameId) const {
    size_t idx = GetIndex(frameId);
    if (_buffer[idx].frameId == frameId) {
        return _buffer[idx].remoteInput;
    }
    return std::nullopt;
}

const GameState* StateBuffer::GetState(uint32_t frameId) const {
    size_t idx = GetIndex(frameId);
    if (_buffer[idx].frameId == frameId && _buffer[idx].state.isValid) {
        return &_buffer[idx].state;
    }
    return nullptr;
}

void StateBuffer::AdvanceConfirmedFrame(uint32_t confirmedFrameId) {
    if (confirmedFrameId > _confirmedFrame) {
        _confirmedFrame = confirmedFrameId;
    }
}

std::optional<uint32_t> StateBuffer::GetLatestConfirmedStateFrame() const {
    // 現在の confirmedFrame から遡り、State が保存されているフレームを探す。
    // 現実の実装では、_confirmedFrame 地点に必ずStateがあるとは限らないため過去へ走査する。
    for (uint32_t f = _confirmedFrame; f >= _oldestFrame; --f) {
        size_t idx = GetIndex(f);
        if (_buffer[idx].frameId == f && _buffer[idx].state.isValid) {
            return f; // 見つかった
        }
        if (f == 0) break; // unsigned underflow防止
    }
    return std::nullopt;
}

} // namespace cccaster::sync
