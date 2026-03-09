#pragma once
/**
 * @file GamePhaseDetector.hpp
 * @brief MBAACC ゲームフェーズ検出 — メモリ読み取りによるゲーム状態判定
 *
 * 【責務】
 *   MBAAのゲームモードID（CC_GAME_MODE_ADDR）を読み取り、
 *   ネットワーク同期で必要なフェーズ（CharaSelect, Loading, InGame 等）を判定する。
 *
 * 【設計上の位置づけ】
 *   Domain 層に属する。MBAAのゲームモードID値に依存しており、
 *   ゲーム非依存の Core 層には置けない。
 *
 * 【旧パス】
 *   cccaster/core/memory/GameMonitor.hpp (転送ヘッダとして互換性維持中)
 *
 * 【入出力】
 *   入力: MBAAプロセスメモリ（CC_GAME_MODE_ADDR, CC_INTRO_STATE_ADDR）
 *   出力: GamePhase enum, bool判定値
 */

#include <cstdint>

namespace cccaster::game_interface {

    /**
     * @brief ネットワーク対戦で同期が必要なゲームの主要なフェーズ
     *
     * @details
     *   CC_GAME_MODE_ADDR の生値を基に変換される。
     *   各フェーズで同期処理（入力遅延、ロールバック、スキップ）が異なるため、
     *   シーンマネージャが参照する判定基準となる。
     */
    enum class GamePhase {
        Unknown,      ///< 不明な状態（初期化前、遷移中など）
        MainMenu,     ///< メインメニュー（GameMode: 2, 4, 6, 8, 9, 10, 16, 18, 19, 25, 26）
        CharaSelect,  ///< キャラクター選択画面（GameMode: 20）
        Loading,      ///< ロード中（GameMode: 21）
        InGame,       ///< 対戦中（GameMode: 22）
        Rematch       ///< リマッチ画面（GameMode: 23）
    };

    /**
     * @brief MBAAのゲーム状態を監視・判定するユーティリティクラス
     *
     * @details
     *   メモリアドレスの直接読み取りを隠蔽し、安全な判定インターフェースを提供する。
     *   全メソッドは static で、状態は持たない（メモリ読み取りのみ）。
     *
     *   【使用メモリアドレス】
     *     - CC_GAME_MODE_ADDR: ゲームモードID (uint32_t)
     *     - CC_INTRO_STATE_ADDR: ラウンドイントロ状態 (uint8_t)
     *     - CC_ROUND_TIMER_ADDR: ラウンドタイマー (uint32_t)
     */
    class GameMonitor {
    public:
        /**
         * @brief CC_GAME_MODE_ADDR から現在の生のゲームモードIDを取得
         * @return uint32_t ゲームモードID（0〜26程度の範囲）
         */
        static uint32_t GetRawGameMode();
        
        /**
         * @brief 現在のゲームモードを GamePhase enum に変換して返す
         * @return GamePhase 現在のフェーズ
         * @details
         *   GetRawGameMode() → switch で振り分け。
         *   未知のモードIDは GamePhase::Unknown を返す。
         */
        static GamePhase GetCurrentPhase();

        // --- 画面判定ヘルパー関数群 ---

        /// @return true: メインメニュー画面にいる
        static bool IsInMainMenu();

        /// @return true: キャラクター選択画面にいる
        static bool IsInCharaSelect();

        /// @return true: ロード中
        static bool IsLoading();

        /// @return true: 対戦中
        static bool IsInGame();

        /// @return true: リマッチ画面にいる
        static bool IsInRematch();

        // --- ラウンド状態判定ヘルパー ---

        /**
         * @brief CC_INTRO_STATE_ADDR からイントロ状態を取得
         * @return uint8_t イントロ状態値 (0=イントロ前, 1=イントロ中, 2=イントロ完了)
         */
        static uint8_t GetIntroState();

        /**
         * @brief ラウンドがアクティブ（プレイ可能）状態かを判定
         * @return true: イントロ完了（introState==2）かつタイマーが動作中
         * @details
         *   同期のディレイ/ロールバック方式の切り替え判定に使用。
         *   イントロ中やKO演出中はfalseを返す。
         */
        static bool IsRoundActive();
    };

} // namespace cccaster::game_interface
