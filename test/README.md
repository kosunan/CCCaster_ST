# Steam版の実ゲームテスト

## 手動3窓

`runtime/3窓起動.bat`で対戦2窓（MBAACC_1/2）と観戦1窓（MBAACC_3）を起動します。ローカル直接接続、模擬遅延・損失なし、手操作が既定です。起動中の対象があれば中止します。

`-Lag`で片道60〜96ms・損失5%、`-LagMinMs` / `-LagMaxMs`で値を指定できます。`-CheckOnly`は事前確認だけ。INIを保持し、共通deployでCLI・GUI・DLL・ステージ画像を揃えます。ゲームは時間制限で自動終了しません。[操作方法](runtime/手動テストの使い方.md)。

## 自動検証

先にルートの`build.bat`と`deploy.bat`を成功させます。検証で起動したプロセスだけを終了し、INI・EXEを前後照合します。基本の合格条件は各比較1000以上の確定F、差分・欠落ゼロ、ロールバック発生、`result.json`の`passed=true`・`protected_unchanged=true`です。

```powershell
python -X utf8 src/src/harness/run_p2p_smoke.py --real-game --standby-spectator --seconds 75
python -X utf8 src/src/harness/run_stage_rematch.py random --spectator --full-intro --seconds 95
python -X utf8 src/src/harness/run_stage_rematch.py fixed --spectator --seconds 80
python -X utf8 src/src/harness/run_stage_rematch.py character --spectator --seconds 80
python -X utf8 src/src/harness/run_rml_gui_smoke.py --seconds 40
build/virtual-pad-venv/Scripts/python.exe -X utf8 src/src/harness/run_stage_selection.py --stage 55 --steam-input-slot 1
build/virtual-pad-venv/Scripts/python.exe -X utf8 src/src/harness/run_training_fn_probe.py --steam-input-slot 1
```

P2P・再戦・GUIはローカル通知サーバーを使用。P2P・再戦は片道15〜25ms・損失5%、条件達成で早期終了します。再戦はゲームスレッド上のHP・タイマー注入で決着に進めるので、自然な試合長の確認とは区別します。

`test/logs`に結果を保存します。短い同一PC試験を全キャラ・実回線・長時間の安定性認定には使いません。vB由来の旧比較用ベンチは別版のアドレスを含むため、Steam用の解決処理を確認せずに実行しないでください。
