# Steam版の現状

2026-10-05。対象はMBAACC Steam 2017-01-05、32bit。共通実装の基準を`CCCaster_verB`の`862f759`（v1.4.0）へ更新しました。ゲーム・アドレス・録画構造はSteam専用です。

## 実装済み

| 項目 | 現行実装 |
|---|---|
| 共通ロジック | vB 1.4のP2P、RmlUi GUI、公開／非公開マッチング、独立入力時計、予測・再送・訂正、観戦、再戦、HUD、Training |
| GUIの版表示 | 左上に`CCCaster verSTEAM`を表示 |
| D / R | D0〜8、既定2・対戦中固定。Rは通常7F、一時予測上限20Fの自動管理。平均遅延には減速で対応 |
| 版・通信分離 | Steam EXE/text照合、実ロード先＋RVAでASLR維持。STN1/CCS1・通信版10。P2P鍵／マッチング領域／観戦Hello／IPCをSteamへ分離 |
| 保存・復元 | 既存Steamの122断片＋3000子ノード、資源寿命ガード、演出RNG全語、Steam録画カーソルを維持 |
| 起動 | vB 1.4の明示DLL初期化・ビルドID照合。Steamのシステム情報呼出し／DDS読込／メニュー分岐を検証付きで処理。起動同期待機30秒 |
| 再戦・観戦 | RANDOM ONCEで再抽選、Steamの7命令パッチを適用・復元。観戦待機・ロード／イントロの描画制御を移植 |
| ステージ | 55ステージ＋RANDOMの除外解除、55/57/58の画像9枚、両儀BGMとボス暗転補正。表と命令をSteam実メモリで照合 |
| Training | vB 1.4の保存・停止・復元、録画や操作設定の維持、DUMMY先頭再生／RANDOM抽選／録り直しをSteamの構造へ接続 |
| 配置・手動試験 | `deploy.bat`で3窓のCLI・GUI・DLL・画像を配置。`test/runtime/3窓起動.bat`は対戦2窓＋観戦1窓、ラグなし・手操作が既定 |

## 確認済み

- コミット前の最終内容：32bit Release・C++56件、3環境への配置、P2P対戦＋観戦待機に成功。対戦／観戦とも1,057確定Fで差分・欠落0、ロールバック15回、INI・EXE18ファイルを保持。最終ビルドID `5ab99f3e39964add97a208e197b695416d9b094a1c42adf3a8d1e36942e1ad56`。SHA-256・条件・根拠は`test/logs/precommit_verification_20261005.json`、`test/logs/p2p_real_20261005_141241_556516/result.json`。
- GUIの`verSTEAM`表記変更後もビルド・C++56件に成功し、3環境のCLI／GUI／DLLを更新。`test/logs/gui_versteam_build_20261005.log`、`test/logs/deploy_20261005_135752_914.json`。
- MinGW32 Release、C++ **56/56成功**。実EXEのASLR再配置、起動・再戦・描画命令、録画の終端からの再生開始も検証。`test/logs/vb14_build_20261005_fifth.log`。
- Pythonは一式120件中119件成功。残りは観戦模擬サーバーの旧版識別値で失敗し、Steam値へ修正後の関連4件すべて成功。`vb14_python_20261005.log`、`vb14_python_spectator_20261005.log`。
- 6文字P2P＋観戦待機：対戦1,066F、観戦1,000F以上で入力・代表状態・メモリ指標の差分／欠落0、ロールバック発生。`test/logs/p2p_real_20261005_130551_900513/result.json`。
- RANDOM ONCE＋全イントロ＋観戦：対戦**1,653F**、観戦**1,651F**が一致。再抽選、7命令の適用／復元、観戦の描画切替とイントロ待機に成功。`test/logs/stage_rematch_random_20261005_131041/result.json`。
- RmlUi GUI：募集・承諾・対戦2窓・観戦1窓・終了後の同一コード待受復帰。対戦1,295F、観戦1,297Fが一致。`test/logs/rml_gui_20261005_131959/result.json`と`sync_and_spectator.json`。
- 最終CLI修正後の固定ONCE＋観戦：対戦1,061F、観戦1,059Fで差分／欠落0、固定ステージ59を維持。`test/logs/stage_rematch_fixed_20261005_132513/result.json`。
- 実バッチから3ゲームの同期完了・キャラ選択到達、起動中の追加起動拒否、INI保全。`test/logs/triple_test_20261005_132409_578/verification.json`。
- 3ゲームの背景存在表56項目、Training/Versus除外解除、両儀BGM16B補正を読取り確認。`test/logs/steam_analysis/vb14_stage_runtime_20261005.json`。

同期試験は同一PC、独立した3コピー、片道15〜25ms・損失5%。再戦はゲームスレッド上のHP・タイマー注入で決着を短縮しています。保存領域全体・全キャラ・全技の完全性や長時間の安定性を認定した結果ではありません。

Trainingの最新実機試験は、仮想パッドのSteam Input個体番号変更と前面喪失で不合格です。途中の保存・停止は観測できましたが、1.4の通常／DUMMY全操作を確認済みとは扱いません。[未確認範囲](OPEN_ISSUES.md)・[移植と解析記録](design/2026-10-05_vb14_steam_migration.md)。

バイナリ3点・配置先9点のSHA-256と各試験の集約は`test/logs/vb14_port_verification_20261005.json`。GUI/RANDOM試験後のCLI環境指定修正は、最終バイナリの固定ONCE・P2P観戦・手動直接接続で確認しています。
