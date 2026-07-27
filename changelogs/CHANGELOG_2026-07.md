# chore: 編集時に単体テストを自動実行する PostToolUse フックを追加

## 2026-07-27: 検証ループの自動化

### 概要
`src/core_dll/` または `src/tests/` 配下を編集した直後に単体テストを自動実行し、
失敗を即座にフィードバックするフックを追加した。あわせて現行ビルドの
E2E 動作確認を行った。

### フックの構成
- `.claude/settings.json` — `PostToolUse` / matcher `Write|Edit`、timeout 120秒
- `.claude/hooks/ctest.sh` — 本体
  - 編集ファイルのパスを見て、対象外なら即 `exit 0`（他プロジェクトの編集では何もしない）
  - テスト実行ファイルのみビルド（DLL 本体はビルドしないので数秒で終わる）
  - `ctest` 実行。ビルドまたはテストが失敗したら `exit 2` で stderr を返す
  - この環境に `jq` が無いため、stdin の JSON は `grep`/`sed` で読む

`dual_test.bat` はフックにしていない。1回80秒かかりゲームウィンドウが2つ起動するため、
編集のたびに走らせるのは現実的でない。E2E は区切りごとに手動で回す。

### 検証
故意にアサートを1つ壊してフックを実行し、`exit=2` で失敗テスト名・ファイル・行番号が
返ることを確認したうえで復旧した。対象外パス（別プロジェクト）で即 `exit 0` することも確認済み。

### 現行ビルドの E2E 結果（参考）
デプロイ後に2窓テストを70秒実行した結果:

| 確認項目 | 結果 |
|---|---|
| 接続・時刻同期 | 成立（θ=1μs、`Mode -> Counting`） |
| 画面停止・高速動作 | **発生せず** — 3840フレーム/約64秒 = 60fps |
| Peer 切断 | なし（70秒間 `alive=1`） |
| 進行 | CharaSelect で停止（入力パイプライン撤去のため期待どおり） |

3月11日版で観測された暴走（`peerF=12551` によるもの）は再現しなかった。

### 変更ファイル
- [NEW] `.claude/settings.json` — PostToolUse フック定義
- [NEW] `.claude/hooks/ctest.sh` — ビルド + ctest 実行スクリプト

---

# docs: InGame 画面停止の原因を特定 — peerFrame のフェーズ跨ぎ汚染を記録

## 2026-07-27: dual_test.bat 実行ログの解析

### 概要
`dual_test.bat` を実行して得た症状「対戦開始で画面が止まり、音楽だけ鳴り、
裏で高速動作する」の原因をログから特定し、設計書に必須修正項目として記録した。
コード変更は伴わない。

### 実行されたバイナリについて
`dual_test.bat` はデプロイを行わないため、実行された DLL は 2026-03-11 04:21 の
ビルド（CB撤去前）だった。したがって本ログは B-1 の検証結果ではなく、
凍結時点の症状の観測データである。

### 特定した因果連鎖
1. CharaSelect 中に MENU 用フレームカウンタが 12551 まで進む
2. `SyncCodec::_latestPeerFrame` は受信 baseFrame の単調最大値で、
   フェーズ遷移ではリセットされない（`Reset()` は `Initialize()` からのみ）
3. InGame 突入で MatchInputBuffer は 0 にリセット、MATCH パケットの baseFrame も 0 から
4. `GetLatestPeerFrame()` は 12551 を返し続ける
5. `gap = 12551 - 0` → `skipWait=true` でメトロノーム待機スキップ（高速化）
6. `SetRenderSkipByGap(12551)` → `OnPresentSkip()` が `Present()` をスキップ（画面停止）
7. 音声は D3D と無関係なので鳴り続ける

証拠: `peerF=12551` が InGame 最初のログ行から最後まで一定。
ログ末尾 `whMatch=12360` は解除直前の状態。

### 「最終ラウンドが終わらない」について
InGame 中の入力は全フレーム 0（非ゼロ入力2130件はすべて CharaSelect 中の操作）。
両者棒立ちのためラウンド1・2とも 5513 フレームちょうどでタイムオーバーし、
決着がつかず永久にループする。ヘッドレステストの必然でありバグではない。

### 現状の影響
CB撤去により gap 計算が消え `SetRenderSkipByGap(0)` 固定になっているため、
現在のツリーではこの症状は再現しない。ただし原因は `SyncCodec` に残存しており、
入力パイプライン再構築で gap 制御を戻した時点で再発する。

### 変更ファイル
- [MODIFY] `docs/design/core_dll/GameMemory_Seam.md` — §5 に必須修正項目として追記
- [MODIFY] `AGENTS.md` — `dual_test.bat` がデプロイしない罠を追記
- [ADD] `build_logs/2026-07-27_dual_test/` — 実行ログ2本と 3/11 版 DLL を保全（gitignore対象）

---

# fix: 入力を GameInput 型に統一 — Rematch 自動ナビが方向をボタンとして書く不具合を解消

## 2026-07-27: B-1 入力の型付け

### 概要
入力を生の `uint32_t` で持ち回るのをやめ、`GameInput { direction, buttons }` 型に統一。
符号化の混在によって Rematch の自動ナビが機能していなかった問題を、
型で表現できないようにすることで解消した。seam 導入（B-2）の前提となる変更。

### 直した不具合
`MatchScene::HandleAutoNavigation()` は「下」を `0x0002`、「上」を `0x0001` として返し、
呼び出し側がシフトせず `GC::WriteInput()` に渡していた。
`FrameControl::WriteInput` は `direction << 16 | buttons` を期待するため:

| 意図 | 実際に書かれていた値 |
|---|---|
| 下 (direction 2) | direction=0, buttons=`0x0002` = `CC_PLAYER_FACING` |
| 上 (direction 8) | direction=0, buttons=`0x0001` = `CC_BUTTON_START` |
| 決定 | `0x0410` — 正しく動作していた |

カーソルが動かず、その場の項目を確定していた。双方が別項目を選ぶため画面がずれる。
`SceneFastBoot` は同じ規約を正しく実装しており（`dirBits << 16`）、
どちらが規約かの判断材料になった。

### 混在していた3つの符号化
| # | 符号化 | 状態 |
|---|---|---|
| 1 | `direction << 16 \| buttons`（テンキー表記） | 実際の規約。`GameInput::Pack()` に一本化 |
| 2 | `BIT_UP=0x01 / BIT_DOWN=0x02` のビットマスク | 削除（Rematch がこの前提で書かれていた） |
| 3 | `COMBINE_INPUT` = `direction \| buttons << 8` | 削除（使用箇所ゼロ） |

### 変更ファイル
- [NEW] `mbaa_mem/GameInput.hpp` — `GameInput` 型 + `Dir::` テンキー定数 + `Pack`/`Unpack`
- [NEW] `src/tests/test_game_input.cpp` — 符号化と Rematch ナビの回帰テスト（23チェック）
- [MODIFY] `engine/FrameControl.hpp` — `WriteInput` / `ClearInput` を `GameInput` 経由に
- [MODIFY] `engine/MatchScene.cpp` — `HandleAutoNavigation` の戻り値を `GameInput` に。
  `ResolveMenuSelection` は `.buttons` を参照。`HandleMenuGate` の未使用引数を削除
- [MODIFY] `engine/SceneFastBoot.cpp` — 4箇所の `WriteInput` を型経由に
- [MODIFY] `mbaa_mem/MbaaInputDefs.hpp` — `BIT_*` / `COMBINE_INPUT` / `RETURN_MASH_INPUT` 削除
- [MODIFY] `src/tests/CMakeLists.txt` — `test_game_input` 追加
- [NEW] `docs/design/core_dll/GameMemory_Seam.md` — seam 設計書（B-1〜B-4 の段取り）

### 検証状況
`ctest` 3スイート98チェック通過、DLL/EXE ともビルド成功。
ただし**実ゲームでの確認は未実施**。Rematch の修正が実際に効くことは
ハーネス（B-3）が立つまで確認できない。現時点の主張は「型として正しくなった」まで。

---

# test: 依存ゼロの単体テスト基盤を新設 — 入力バッファと NetplayClock の挙動を固定

## 2026-07-27: L1 テスト基盤の構築

### 概要
入力パイプライン再構築の前段として、ゲームを起動せずに実行できる単体テスト基盤を
新設した。`ctest` を有効化し、依存ゼロで検証できる2モジュールの現在の挙動を
特性化テストとして固定。全75チェックが通過。

### 変更理由
凍結の直接原因は個別バグではなく「仮説を検証する手段が実ゲーム2窓の35秒目視しか
なかったこと」。修正の正否を確認できないまま次を書く状態を先に解消する。

### テスト方針
- 外部フレームワークを導入しない（mingw32 環境で依存を増やさない）。
  `test_support.hpp` に最小のアサートマクロを置き、外部シンボルは `stub_*.cpp` で置換
- ケース名が `[HAZARD]` で始まるものは**現在の危険な挙動をそのまま固定**したもの。
  緑であることは「正しい」ではなく「変わっていない」を意味する

### 固定した挙動のうち危険なもの
- `MatchInputBuffer`: 確定済みスロットへの再確定はミスマッチとして検出されない
  （冗長入力は毎パケット再送されるため、デシンクが黙って通過しうる）
- `MatchInputBuffer`: `RING_SIZE`(600F) 周回時にフレーム番号を検証せず別フレームを返す
- `MatchInputBuffer`: `ConfirmRemote` が `slot.frame` を更新しない
- `MatchInputBuffer` / `NetplayClock`: 「なし」をセンチネル 0 で表すため
  フレーム0のミスマッチ・開始時刻0が「未設定」と区別できない
- `NetplayClock`: RTT 同値ではθを新しいサンプルに乗り換えない（strict less-than）
- `NetplayClock`: `SetPeerStartTime` のθ変換は呼出し時点で固定され、後から再計算されない
- `NetplayClock`: `Reset()` が `_baselineTheta` を消さない

### 変更ファイル
- [NEW] `src/tests/test_support.hpp` — 依存ゼロの最小テストハーネス
- [NEW] `src/tests/test_input_buffers.cpp` — MatchInputBuffer / MenuInputBuffer（43チェック）
- [NEW] `src/tests/test_netplay_clock.cpp` — θ推定・α補正・開始時刻合意（32チェック）
- [NEW] `src/tests/stub_wasapi_clock.cpp` — WasapiClock::GetTimeUs() の置換スタブ
- [MODIFY] `src/tests/CMakeLists.txt` — 2ターゲット追加 + `add_test` 登録
- [MODIFY] `CMakeLists.txt` — `enable_testing()` 追加
- [MODIFY] `AGENTS.md` — テスト実行方法と `[HAZARD]` の扱いを追記

---

# docs: AI向け指示ファイルを落とし穴ベースに再構成 — AGENTS.md 圧縮とガイド統廃合

## 2026-07-27: 指示ファイルの再構成

### 概要
`AGENTS.md` (9KB) と `AI_WORKSPACE_GUIDE.md` (19KB) の二重管理を解消し、
AI 向け指示を `AGENTS.md` 単一ファイル (4.3KB) に集約。内容も「コードを読めば
わかること」を全削除し、「コードを読んでもわからないこと」だけを残す方針に転換。

### 変更理由
- 両ファイルが揃って「最初に読め」と主張し、指示が競合していた
- `AI_WORKSPACE_GUIDE.md` のディレクトリツリーが実在しない構成
  (`src/app/`, `domain_netplay/`, `domain_scene/`, `src/cli/`) を説明しており、
  参照した AI が誤った前提で作業を始める状態だった
- ディレクトリ別の「規範/禁止」9セクションは、コードの配置を見れば導ける内容だった

### AGENTS.md に残した内容（プロジェクト固有の暗黙知）
- 凍結時点の状態 — 入力パイプラインは意図的に撤去済みで、未実装であってバグではない旨
- `SceneRunner::Step()` でのブロック禁止 — キープアライブが不正化し `Peer Disconnected` に至る
- `CC_INTRO_STATE_ADDR` の値の向き (2=紹介中 / 1=pre-game / 0=in-game)
- `CC_SKIP_FRAMES_ADDR` 使用禁止
- フォルダ名と namespace の対応表（過去のリネームで乖離）
- mingw32 (32bit) 必須 — mingw64 では DLL 生成に成功したうえで注入だけ失敗する
- `docs/` は現行仕様ではなく経緯の記録である旨、`tools/` (DummyPeer) の消失
- `.agents/` およびルート直下の `*.txt` / `*.log` / `*.exe` が gitignore 対象である旨

### 変更ファイル
- [MODIFY] `AGENTS.md` — 全面書き換え（9KB → 4.3KB）
- [NEW] `CLAUDE.md` — `@AGENTS.md` の1行のみ（内容の重複を作らない）
- [MOVE] `AI_WORKSPACE_GUIDE.md` → `docs/archive/AI_WORKSPACE_GUIDE_2026-03.md`
- [MODIFY] `README.md` — 実在しない `tools/` の記述を削除、`src/cli_launcher` に修正、
  `docs/` が現行仕様と乖離している旨を追記
- [MODIFY] `.agents/workflows/build.md` — デプロイ先を実在する `MBAACC_1` / `MBAACC_2`
  の2窓構成に修正（gitignore 対象のため本コミットには含まれない）
