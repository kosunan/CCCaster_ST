# 2026年10月

## 2026-10-05 vB 1.4のネットコードをSteam版へ移植する

- vB 1.4の共通ロジックとフォルダー構成をSteam版へ移植し、Steam専用のアドレス・保存復元・起動処理へ接続。
- 不要な旧構成を整理し、対戦2窓＋観戦1窓の起動バッチを追加。GUI左上の表記を`verSTEAM`へ統一。
- コミット前の差分検査で検出した移動元由来の行末空白を整理し、最終32bit Releaseビルド・C++56件を再確認。
- 最終ビルドのP2P対戦＋観戦待機試験に成功。片道15〜25ms・損失5%、対戦／観戦とも1,057確定Fで差分・欠落0、ロールバック15回、INI・EXE18ファイルを保持。
- 根拠：`test/logs/precommit_verification_20261005.json`、`test/logs/p2p_real_20261005_141241_556516/result.json`。製品・判定器など532入力ファイルのSHA-256が試験前後で一致。
- 詳細と既知の未確認範囲は以下の変更記録および[現状](../CURRENT_STATE.md)・[未確認範囲](../OPEN_ISSUES.md)を参照。

## 2026-10-05 GUI左上の版表示をverSTEAMへ変更する

- GUI左上のブランド表記を`CCCaster verSTEAM`へ変更。
- 32bit Releaseのビルド・C++56件に成功。テスト用3環境へ同一ビルドのCLI／GUI／DLLを配置し、SHA-256を照合。
- 根拠：`test/logs/gui_versteam_build_20261005.log`、`test/logs/deploy_20261005_135752_914.json`。

## 2026-10-05 vB 1.4の共通ロジックをSteam版へ移植する

- vB 1.4（862f759）のGUI、P2P、マッチング、D固定／R自動管理、RANDOM再戦、観戦、Trainingを統合。
- SteamのASLR、メモリ保存表、資源寿命、リプレイ構造を保ち、新しいゲーム依存箇所を実EXE・実メモリで解析して接続。
- 不要な旧UI・直接入力ヘッダー・専用診断を除去し、vBの構成へ整理。移植前の未コミット変更を退避。
- CLI環境指定と起動待機を修正。3窓バッチ・配備処理をCLI／GUI／DLL・追加画像へ対応させ、INIを保持。
- C++56件、実対戦／観戦／RANDOM・固定再戦／GUI／3窓起動の確認結果と、Training等の未確認範囲を記録。

[現状](../CURRENT_STATE.md)・[解析と検証](../design/2026-10-05_vb14_steam_migration.md)・[未確認範囲](../OPEN_ISSUES.md)。
