# vB 1.4のSteam移植

2026-10-05。vB `a414a39`を共通祖先とし、読取り元`CCCaster_verB/862f759`と移植前Steamの未コミット変更を3方向で統合。元vBを編集せず、Steamのゲーム、INI、リプレイ、既存変更を保全しました。

## 保全と構成

`archive/steam_before_vb14_20261005/pre_port.zip`に580ファイル、`manifest.json`にSHA-256を保存。`base`、`steam_diff`、`merged`、`merge_report.json`に祖先・差分・マージ結果を記録しました。廃止UI、古いControllerMapper、未使用InputInjectorとカニファン専用StartupProfileを除去。開発設定は`src`、実装は`src/src`、画像は`src/assets/GRP`へ集約しています。

vBのRmlUi GUI、暗号化6文字P2P、マッチング、D固定／R自動管理、再戦・観戦・Trainingロジックを採用。Steamの保存表、資源寿命、ゲーム入力、音声、リプレイserializerは既存解析を維持しました。

## ゲーム識別と移植点

対象EXE SHA-256：`11270cf2da851054aa6c1bff309d50b042429abd936dca5df833bcd1de5e6c46`。PE32、image size `0xF3E000`。下表はpreferred VAです。実行時は`GameRuntime::Preferred`で実ロード先＋RVAへ変換します。

| 用途 | Steam preferred VA・根拠 |
|---|---|
| 起動時システム情報 | `4F0CFF -> 4F0FA0`。WM_INITDIALOG、ECXにHWND。`4F0CF0..4F0D07`全24Bを照合し、最初のゲームフレームでcallを復元 |
| ステージ除外表 | Training `5B4E00`、Versus `5B4E30`。元の3/19件を照合して終端値へ置換 |
| 背景存在表 | `7B6230`。100 DWORD、55背景＋RANDOMを実ロード後に確認 |
| 両儀BGM | `7CFC78 = 7CF228 + 55 * 0x30`。BgList初期化`520CFA`、ループ位置読出し`5215B4`。元16Bからbgmme55と対応ループ位置へ変更 |
| ボス暗転キー | `58AD8C`のIsGiantStage、12B照合 |
| ランダム抽選 | `486260`の関数プロローグを照合。ホストの決定を双方・観戦へ共有 |
| 再戦の自動選択 | `493687`、状態・待機分岐`493655..4936F9`を解析。決定フック`4359AF`、ステージ確定`47FFED`は既存Steam解析を継承 |
| RANDOM再戦7命令 | `4945A7`、`481841`、`481C58`、`482593`、`4E5690`、`4E5683`、`4800FA`。戻り先・レジスタ・ASLR付きSSE参照を照合 |
| イントロ重ね描き | `46E9B0`。ロード側`4CB434`からの呼出しを確認。類似候補`477760`は用途が異なるため不採用 |
| ボーダーレス比率 | GetClientRectの呼出し`48AAA3`、戻り先`48AAA9`。この戻り先だけ描画用サイズを返す |
| Training録画 | Container `0x1C`、Round `0x138`、inputs `+0x11C`、frameInState `+0x18`。終端後のDUMMYも録画を削らず先頭へ戻す |
| DUMMY再抽選 | `4D0130`、選択`7B40E0`、設定`7E9EC0`、現在round `7E9C08`。抽選後の演出RNG57 DWORDを保存復元の外へ保持 |
| 録り直し | 標準クリア`49C4CA/49C4D9`に従いRound `+0x128`と`+0x80`を初期化。現在の4キャラ操作モード`5BC377 + n*0xAFC`を維持 |

読み取り専用の逆アセンブル記録は`test/logs/steam_analysis/vb14_sites_20261005.asm`。実メモリのステージ補正は同フォルダーの`vb14_stage_runtime_20261005.json`。C++の`test_steam_port`が実EXEから命令署名・ASLR参照を確認します。

## 接続と起動

- IPCは`Local\CCCasterSteam_SharedState`、magic `0xCC510004`。観戦HelloのゲームIDは`0x20170105`。形式版10とSteam STN1/CCS1は維持。
- P2P鍵導出は`CCCaster_Steam-v1`、マッチング領域は`CCCaster_Steam-matching-v1`。カニファンと募集・暗号鍵を共有しません。テストベクトルは独立したPython計算で照合。
- DLLロードと初期化を分離し、同じビルドIDのCLI／GUI／DLLを要求。SteamのEXE/text照合はDLL側でも維持。
- 初回3窓試験はデバイス作成後に15秒のランチャー期限へ達したため不合格。単独起動ログで約13秒の初期化を確認し、起動待機をゲーム側の30秒方針へ統一。さらに上記のシステム情報スキップをSteamへ移植。
- CLIの`--legacy-host`等がWin32環境だけを書き換え、CRTの`getenv`に反映されない問題を修正。`_putenv_s`へ統一し、`--sim-delay/--sim-loss`もDLLへ環境を継承。手動3窓バッチで直接接続を確認。

## 検証と制限

[現状](../CURRENT_STATE.md)の各ログを参照。最終コードではC++56件成功、P2P／観戦／RANDOM ONCE／イントロ描画／GUIと手動3窓を確認しました。全代表指標の比較成功を全メモリの保存完全性と呼びません。

Trainingとステージ一覧の仮想パッド試験は実行したものの、Steam Inputの個体番号・前面状態の条件で完了できませんでした。前面喪失後の入力が0になるログと試験失敗を保全し、しきい値を下げたり不合格を成功へ置き換えたりしていません。DUMMY/RANDOM・録画中の全実操作は未確認として残します。

ZIP作成・コミット・外部公開は行っていません。
