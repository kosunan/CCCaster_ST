# Steam版の開発手順

作業先は`I:/work_space/CCCaster_Steam`、ブランチ`codex/steam-port`。読取り元は`I:/work_space/CCCaster_verB`のv1.4、コミット`862f759`。Steam固有変更は元vBへ戻しません。

## 構成と保全

CMake入口は`src/CMakeLists.txt`、実装・共通契約・テスト・harnessは`src/src`。依存ソースと生成物は`build`、3つのゲームコピーは`test/runtime/MBAACC_1..3`、各ランチャーは`cccaster_st`です。

移植前の未コミット変更は`archive/steam_before_vb14_20261005/pre_port.zip`とSHA-256付き`manifest.json`に保全。v1.3基準との差分・3方向マージ結果も同じ退避先にあります。旧InputInjector、廃止済みUI、未使用のカニファン固定アドレス診断を除去し、開発設定は`src`へ集約しました。ゲーム、INI、リプレイ、既存ログは保全します。

## ビルド・検証

PowerShell 7と`C:/msys64/mingw32/bin`を使用します。別フォルダーのCMakeCacheはコピーしません。

```powershell
./build.bat
./deploy.bat
python -X utf8 -m unittest discover -s src/src/harness -p 'test_*.py'
python -X utf8 src/src/harness/run_p2p_smoke.py --real-game --standby-spectator --seconds 75
python -X utf8 src/src/harness/run_stage_rematch.py random --spectator --full-intro --seconds 95
```

基本実ゲーム試験はローカル通知サーバー、片道15〜25ms・損失5%。`result.json`の`passed`と`protected_unchanged`、各比較1000以上の確定F、差分・欠落0、ロールバック発生を確認します。上限秒に達しただけでは成功にしません。`--test-root`でリポジトリ内の独立コピーを指定できます。

実EXE静的試験には`CCCASTER_TEST_STEAM_EXE`と`CCCASTER_TEST_LEGACY_EXE`を指定します。現環境では設定済みです。

Trainingの通常設定経路による検証は`build/virtual-pad-venv/Scripts/python.exe -X utf8 src/src/harness/run_training_fn_probe.py --steam-input-slot 1`。その時点のSteam Inputの列挙GUIDに合わせた設定を独立コピーだけに作ります。物理パッドの操作確認とは区別します。

逆アセンブルは`python -X utf8 test/scripts/steam_disasm.py steam 4f0cf0:18`。依存pefile/capstoneは`test/logs/steam_analysis/deps`。自動類似候補は誤対応を含むため、命令・呼出規約・データ幅を確認します。新規実機プローブも`steam_runtime.py`でASLRを解決してください。vB由来の旧比較ベンチを未対応のままSteamに流用しません。

[現状](CURRENT_STATE.md)・[テスト入口](../test/README.md)・[解析記録](design/2026-10-05_vb14_steam_migration.md)。
