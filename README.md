# CCCaster Steam 1.4.0

カニファン版 **CCCaster_verB v1.4（862f759）** のネットコードを **MBAACC Steam 2017-01-05 / 32bit** へ移植したプロジェクトです。

共通の通信・入力・ロールバック・GUI・観戦・再戦・TrainingはvB 1.4を基準にし、ゲームアドレス、フック、保存領域、リプレイ構造はSteam版を解析して実装しています。実ロード先＋RVAで解決し、ASLRを維持します。カニファン版とのクロスプレイには対応しません。

## 起動

- GUI：`test/runtime/MBAACC_1/cccaster_st/CCCaster_Steam_GUI.exe`
- 対戦2窓＋観戦1窓：[3窓起動.bat](test/runtime/3窓起動.bat)。同一PC内の直接接続、ラグなし・手操作が既定です。
- 模擬ラグ：`3窓起動.bat -Lag`。遅延値は `-LagMinMs 90 -LagMaxMs 120` のように指定できます。

設定は各`cccaster_st`内の`cccaster_steam.ini`と機器別INIです。F4で機器を選びます。キャラ選択のCtrl+0〜8でDを設定し、対戦中は固定。Rは通常7Fを基準に自動管理し、一時スパイクの予測上限は20Fです。

TrainingはFN1で保存・押下中停止、FN2で読込。DUMMY再生・録画の再開位置と現在の操作設定を扱う1.4の処理をSteamの録画構造へ接続しています。

## ビルドと配置

`build.bat`が32bit ReleaseビルドとC++テストを実行し、`deploy.bat`が3コピーへCLI・GUI・DLL・ライセンス・追加ステージ画像を配置します。ゲームEXEとINIは上書きしません。

```text
src/       CMake入口・assets・src/実装/tests/harness・開発用設定
build/     32bit成果物・依存ソース・生成物
test/      source/参照資料・runtime/独立ゲーム・logs/記録・scripts/解析
docs/      現行仕様・解析根拠・changelogs
release/   配布用置場
archive/   移植前の未コミット変更を含む保全
```

確認済みの条件と未確認範囲は[現状](docs/CURRENT_STATE.md)、[未検証範囲](docs/OPEN_ISSUES.md)、[1.4移植記録](docs/design/2026-10-05_vb14_steam_migration.md)を参照してください。
