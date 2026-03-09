#pragma once
#include <cstdint>
#include <random>

namespace dummy_peer {

// ゲームパッドの入力をランダムに生成する
class InputGenerator {
public:
    InputGenerator();

    // 16bit のランダムなコントローラ入力を生成する
    // (方向キー 4bit + ボタン 8bit + 余り のイメージ)
    uint16_t Generate();

    // ニュートラル (0x0000) を返す（待機フェーズ用）
    uint16_t Neutral() const { return 0; }

private:
    std::mt19937 _rng;
    std::uniform_int_distribution<uint16_t> _dist;
};

} // namespace dummy_peer
