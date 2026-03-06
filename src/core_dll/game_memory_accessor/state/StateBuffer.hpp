#pragma once

#include <vector>
#include <cstdint>
#include <optional>

namespace cccaster::sync {

    // 1フレーム分のアプリケーション状態（生メモリダンプ）を保持するコンテナ
    struct GameState {
        uint32_t frameId;
        std::vector<uint8_t> memoryDump;
        bool isValid;
        
        GameState() : frameId(0), isValid(false) {}
    };

    // 最長128フレームまでの入力とゲーム状態を保持するリングバッファ
    class StateBuffer {
    public:
        // @param bufferSize 保持するフレーム数 (例: 128)
        explicit StateBuffer(size_t bufferSize);
        ~StateBuffer() = default;

        // 指定フレームの入力を保存 (ローカル入力 / リモート入力)
        void SaveLocalInput(uint32_t frameId, uint16_t input);
        void SaveRemoteInput(uint32_t frameId, uint16_t input);

        // 指定フレームのメモリ状態を保存
        void SaveState(uint32_t frameId, const std::vector<uint8_t>& memoryData);

        // 指定フレームの情報を取得
        std::optional<uint16_t> GetLocalInput(uint32_t frameId) const;
        std::optional<uint16_t> GetRemoteInput(uint32_t frameId) const;
        const GameState* GetState(uint32_t frameId) const;

        // 指定フレームより古い情報を破棄してリングバッファを進める（確定フレームの進行）
        void AdvanceConfirmedFrame(uint32_t confirmedFrameId);

        // ロールバック用に直近の確定状態（両者の入力が揃っている最も新しいフレーム）を検索
        // 戻り値: そのフレームIDとデータへのポインタ。見つからなければ nullopt
        std::optional<uint32_t> GetLatestConfirmedStateFrame() const;

    private:
        size_t GetIndex(uint32_t frameId) const;

        struct FrameData {
            uint32_t frameId;
            std::optional<uint16_t> localInput;
            std::optional<uint16_t> remoteInput;
            GameState state;
        };

        size_t _capacity;
        std::vector<FrameData> _buffer;
        uint32_t _oldestFrame;    // バッファに残っている最古のフレーム
        uint32_t _confirmedFrame; // 既に結果が確定し、ロールバックされないフレーム
    };

} // namespace cccaster::sync
