#pragma once
// ============================================================================
// FakeGame — スクリプトされた MBAA
//
// 【役割】
//   実ゲームの代わりに IGameMemory として振る舞い、決められたフレーム数で
//   画面を進める。入力に反応はしない（タイムライン駆動）。
//   ゲーム側の挙動を再現することが目的ではなく、
//   「同期ロジックが画面遷移にどう反応するか」を再現可能にすることが目的。
//
// 【なぜタイムライン駆動か】
//   入力に反応する忠実な模倣を作ると、それ自体が検証対象のない新しいコードに
//   なってしまう。ロード時間を左右で変える等の操作が効けば症状は再現できる。
//
// 【記録】
//   WriteInput() で書き込まれた入力を全フレーム記録する。
//   この記録列を2プロセス間で突き合わせるのが決定性テスト(B-4)の判定材料。
// ============================================================================

#include "core_dll/mbaa_mem/IGameMemory.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cccaster::harness {

class FakeGame final : public cccaster::game_interface::IGameMemory {
public:
    using GameInput = cccaster::game_interface::GameInput;

    /// 各画面の滞在フレーム数。loadingFrames を左右で変えるとロード時間の
    /// ばらつきを再現できる（証言②の再現に使う）。
    struct Script {
        uint32_t mainMenuFrames    = 60;
        uint32_t charaSelectFrames = 180;
        uint32_t loadingFrames     = 60;
        uint32_t introPlayFrames   = 60;   ///< introState=2 の長さ
        uint32_t introPreFrames    = 60;   ///< introState=1 の長さ
        uint32_t roundFrames       = 300;  ///< introState=0 の長さ
        uint32_t rematchFrames     = 120;
        int      rounds            = 2;
    };

    enum class Stage { Boot, CharaSelect, Loading, InGame, Rematch, Finished };

    struct Record {
        uint32_t  frame;
        uint32_t  gameMode;
        uint8_t   introState;
        GameInput p1;
        GameInput p2;
    };

    explicit FakeGame(const Script& script);

    /// 1フレーム進める。SceneRunner::Step() の前に呼ぶ。
    void Advance();

    bool        IsFinished() const { return _stage == Stage::Finished; }
    Stage       CurrentStage() const { return _stage; }
    const char* StageName() const;
    uint32_t    Frame() const { return _frame; }

    const std::vector<Record>& Written() const { return _written; }

    /// 記録を1行1フレームのテキストで書き出す。突き合わせはこのファイルで行う。
    bool DumpTo(const std::string& path) const;

    // ── IGameMemory ──
    bool     IsAvailable()      const override { return true; }
    uint32_t GameMode()         const override { return _gameMode; }
    uint8_t  IntroState()       const override { return _introState; }
    uint32_t WorldTimer()       const override { return _worldTimer; }
    uint32_t RealTimer()        const override { return _realTimer; }
    uint32_t MenuStateCounter() const override { return _menuStateCounter; }
    void     WriteInput(GameInput p1, GameInput p2) override;

private:
    void EnterStage(Stage next);

    Script   _script;
    Stage    _stage            = Stage::Boot;
    uint32_t _framesInStage    = 0;
    int      _roundsPlayed     = 0;

    uint32_t _frame            = 0;
    uint32_t _gameMode         = 0;
    uint8_t  _introState       = 0;
    uint32_t _worldTimer       = 0;
    uint32_t _realTimer        = 0;
    uint32_t _menuStateCounter = 0;

    std::vector<Record> _written;
};

} // namespace cccaster::harness
