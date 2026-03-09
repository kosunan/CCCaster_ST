#pragma once

#include "core_dll/sync/FrameSyncState.hpp"
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstddef>

namespace cccaster {
namespace sync {

    template<typename T>
    class FrameInputSlot {
    public:
        FrameInputSlot() = default;
        ~FrameInputSlot() = default;

        // 指定された index(フェーズ) と frame の入力を取得する。未達なら直前の有効な入力を返す
        T Get(uint32_t index, uint32_t frame) const {
            if (index >= _inputs.size() || _inputs[index].empty()) {
                return LastInputBefore(index);
            }
            if (frame >= _inputs[index].size()) {
                return _inputs[index].back();
            }
            return _inputs[index][frame];
        }

        // 指定された index:frame に単一の入力をセットする（既存の入力を上書き可能）
        void Assign(uint32_t index, uint32_t frame, T targetInput) {
            Resize(index, frame);
            _inputs[index][frame] = targetInput;
        }

        // 指定された index:frame を起点として複数フレーム分の入力を上書きする
        void SetMultiple(uint32_t index, uint32_t frame, const T* data, size_t n, uint32_t checkStartingFromIndex = UINT32_MAX) {
            if (index >= checkStartingFromIndex) {
                IndexedFrame f;
                size_t i;
                for (i = 0, f = { { frame, index } }; i < n; ++i, ++f.parts.frame) {
                    if (Get(f.parts.index, f.parts.frame) == data[i]) {
                        continue;
                    }
                    _lastChangedFrame.value = std::min(_lastChangedFrame.value, f.value);
                    break;
                }
            }
            Resize(index, frame, n);
            std::copy(data, data + n, &_inputs[index][frame]);
        }

        void Clear() {
            _inputs.clear();
        }

        bool IsEmpty() const {
            return _inputs.empty();
        }

        uint32_t GetEndIndex() const {
            return static_cast<uint32_t>(_inputs.size());
        }

        uint32_t GetEndFrame() const {
            if (_inputs.empty()) return 0;
            return static_cast<uint32_t>(_inputs.back().size());
        }

        IndexedFrame GetLastChangedFrame() const {
            return _lastChangedFrame;
        }

        void ClearLastChangedFrame() {
            _lastChangedFrame = MaxIndexedFrame;
        }

    private:
        // index -> frame -> input
        std::vector<std::vector<T>> _inputs;
        IndexedFrame _lastChangedFrame = MaxIndexedFrame;

        void Resize(uint32_t index, uint32_t frame, size_t n = 1) {
            T last = 0;
            if (index >= _inputs.size()) {
                last = LastInputBefore(static_cast<uint32_t>(_inputs.size()));
                _inputs.resize(index + 1);
            } else if (!_inputs[index].empty()) {
                last = _inputs[index].back();
            }

            if (frame + n > _inputs[index].size()) {
                _inputs[index].resize(frame + n, last);
            }
        }

        T LastInputBefore(uint32_t index) const {
            if (_inputs.empty() || index == 0) return 0;
            if (index > _inputs.size()) {
                index = static_cast<uint32_t>(_inputs.size());
            }

            do {
                --index;
                if (!_inputs[index].empty()) {
                    return _inputs[index].back();
                }
            } while (index > 0);

            return 0;
        }
    };

} // namespace sync
} // namespace cccaster
