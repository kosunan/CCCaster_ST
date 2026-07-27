# fix: IntroBarrier がロード時間差で無効化される問題を修正 — 事前通知の削除と到達のラッチ化

## 2026-07-27: 証言②「ロード時間のばらつきでずれる」の修正

### 概要
ハーネス(B-3)で再現した IntroBarrier の不成立を修正した。原因は3つあり、
いずれも「1つのフラグに複数の意味を持たせたこと」に起因する。

### 修正内容

**1. Loading 中の事前通知を削除**
`MatchScene::OnLoading` が Loading 突入時点で `localPhaseReady = true` を立てていた。
このフラグは `HandleRoundStartSync` 側で「intro=2 に到達した」の意味で待たれているため、
ロード時間が左右で違うと、まだ Loading 中の相手からの通知でバリアが解除されていた。

**2. intro=2 の到達をラッチ化（最も影響が大きい）**
`HandleRoundStartSync` は `introState` の瞬間値を見て `!= 2` なら待機していた。
しかし `GC::SetModePause()` は名前に反してゲームを止めない（`SetNormalSpeed()` と
同一実装）。相手を待つ間に自分の intro は 2→1→0 と進むため、
瞬間値判定では先に到達した側が次のラウンドまで条件を満たせなくなる。
1 だけ直すと先行側が待ち続けて事態が悪化するため、到達を `s_reachedIntro2` に
ラッチする形に変更した。

**3. `localPhaseReady` をリセット対象に追加**
`ResetInGame` は「常に送信するため」意図的に `localPhaseReady` を true のまま
維持していた（コメントに明記あり）。これでは前の対戦の到達通知が次のバリアを
素通りさせる。通知はレベル駆動なので、片側が先にリセットしても相手の次の
パケットで復帰する。`ResetLoading` でも両フラグをクリアするようにした。

### 検証（harness, ロード 60F / 240F）

| | 修正前 | 修正後 |
|---|---|---|
| HOST バリア解除 | WT=302（待たず先行） | WT=514 |
| CLIENT バリア解除 | WT=482 | WT=481 |

HOST は intro=2 到達後 212 フレーム待って CLIENT の到達を確認してから解除する。
HOST 514 / CLIENT 481 の 33 フレーム差はランナーが起動を 500ms ずらしている分
（≒30F）であり、両者は同一の実時刻で解除している。

対称ロード（60F/60F）でも退行なし。両者とも frame 300 で intro=2 に到達し、
HOST=WT335 / CLIENT=WT301 で解除、ともに frame 1262 で完走した。
なお修正前は事前通知により待機ゼロだったため、対称条件でも HOST に
30F 前後の待ちが増える。これは相手の実際の到達を待つようになった結果であり、
バリアとして正しい挙動。実対戦では両者がほぼ同時に開始するため待ちは小さい。

### 残る課題（この修正の範囲外）
ゲーム時間そのもののずれ（HOST Rematch=1142 / CLIENT=1322 の 180F 差）は解消しない。
バリアはラウンド開始の論理的な基準を揃えるものであって、先行側を実際に
待たせる仕組みではないため。先行側を止めるには入力の枯渇が必要で、
入力パイプラインの再構築を待つ。

### 変更ファイル
- [MODIFY] `engine/MatchScene.cpp` — `OnLoading` の事前通知削除、
  `HandleRoundStartSync` の到達ラッチ化、`ResetLoading`/`ResetInGame` のリセット範囲拡大

---

# feat: ヘッドレスハーネスを新設 — MBAA 無しで netplay 同期を通しで実行できるようにした

## 2026-07-27: B-3 ハーネスの構築

### 概要
`harness.exe` を新設。`FakeGame`（スクリプトされた MBAA）を `IGameMemory` として
設置し、`SceneRunner::Init/Step` を自前ループで回す。2プロセスを loopback UDP で
繋ぐと、MBAA を一切起動せずに netplay の全ライフサイクルが動く。

### 結果
2プロセス実行で以下がすべて成立した（約21秒、GUI なし）。

```
[SyncCodec] Peer READY received.
[NetplaySession] Mode -> WaitStart (peer READY received)
[Metronome] Started.
[NetplaySession] Mode -> Counting. startTime=... θ=1us startFrame=200
[SceneRunner] Sync completed! θ=1us
[SceneRunner] Phase change: 0 -> 2   (CharaSelect)
[SceneRunner] Phase change: 2 -> 3   (Loading)
[IntroBarrier] Pre-signaling during Loading phase.
[SceneRunner] Phase change: 3 -> 4   (InGame)
[IntroBarrier] Both peers at intro=2! phaseBaseFrame=0 Go!
[SceneRunner] Phase change: 4 -> 5   (Rematch)
finished at frame=1262
```

`IntroBarrier` が両プロセスで揃って発火することを、実ゲーム無しで初めて観測した。

### リンク境界
| 実コード（検証対象） | スタブ（`harness_stubs.cpp`） |
|---|---|
| SceneRunner / MatchScene / SceneInputFilter | HookLog |
| NetplaySession / NetplayClock / SyncCodec | DirectInputHook（入力注入口も兼ねる） |
| NetplayManager / PacketRouter / UdpSocket / NetworkSimulator | StateUiLogic（ImGui を引くため） |
| Metronome / WasapiClock | TimeHooks（MinHook を引くため） |
| PhaseMonitor / GameMemory | SceneFastBoot（後述） |

通信は本物の UDP。パケット組立・θ推定・メトロノーム・IntroBarrier はすべて実コード。

### 構築中に判明した2点

**1. FastBoot は seam の外なのでハーネスで落ちる**
初回実行は Segfault した。原因は B-2 で意図的に seam の外に残した
`CC_GAME_STATE_ADDR` の読み書きと `CC_SFX_ARRAY_ADDR` への 1500 バイト `memset`。
設計どおり `SceneFastBoot` をスタブに置換して解決した。
seam を通していない箇所だけが落ちたため、境界の所在が実行時に確認できた形になった。

**2. 同期前後でフレームのペースを握る主体が入れ替わる**
同期成立前は `Metronome` が停止しており `SceneRunner::Step()` は待たない。
実ゲームでは `Present` が約60fpsの外側ペースを作るが、ハーネスにはそれが無く、
ハンドシェイクの `START_MARGIN_US`(0.5秒) が経過する前に全1262フレームを
走り切って一度も接続しなかった（`synced=0`、ログ58行）。
`timeBeginPeriod(1)` + QPC のフレームリミッタを入れて解決。

### ハーネス初日の成果: 証言②「ロード時間のばらつきでずれる」を再現した

`--HostLoadingFrames 60 --ClientLoadingFrames 240` で左右のロード時間を変えたところ、
IntroBarrier が機能していないことが確認できた。

| | ロード | InGame 到達 | IntroBarrier 解除 |
|---|---|---|---|
| HOST | 60F | frame 300 | WT=302 |
| CLIENT | 240F | frame 480 | WT=482 |

HOST は CLIENT の到達を待たず 180 フレーム先行した。以降ずれたまま復帰せず、
Rematch 到達も HOST=1142 / CLIENT=1322 と 180 フレーム離れたままだった。

**原因**: `MatchScene::OnLoading` の事前通知。Loading 突入時点で
`localPhaseReady = true` を立てるため、CLIENT はまだ intro=2 に到達していないのに
「準備完了」を送信する。HOST 側の `peerPhaseReady` が真になり、バリアを素通りする。

コメントには「InGame 到達時にはバリア待機ゼロを実現」と意図が書かれているが、
ロード時間が左右で異なる場合はバリアそのものが無効化される。
`localPhaseReady` が「Loading に入った」と「intro=2 に到達した」の
2つの意味を兼ねていることが本質。

### 現時点の限界
記録された入力は **0件**。入力パイプラインが撤去済みで、ゲームに書き込む処理が
存在しないため。したがって決定性テスト(B-4)の「入力列の突き合わせ」は、
入力パイプラインを再構築するまで比較対象が空のままになる。
一方、上記のようにフェーズ遷移とバリアの検証は入力パイプライン無しで行える。

### 変更ファイル
- [NEW] `src/harness/FakeGame.hpp/.cpp` — スクリプトされた MBAA + 書込み記録
- [NEW] `src/harness/harness_main.cpp` — 引数処理・設置・フレームループ
- [NEW] `src/harness/harness_stubs.cpp` — HookLog / DirectInputHook / StateUiLogic / TimeHooks / SceneFastBoot
- [NEW] `src/harness/run_pair.ps1` — 2プロセス起動と記録の突き合わせ用ランナー
- [NEW] `src/harness/CMakeLists.txt`
- [MODIFY] `CMakeLists.txt` — `add_subdirectory(src/harness)`

---

# refactor: ゲームメモリ seam (IGameMemory) を導入 — 同期ロジックをゲーム無しで検証可能に

## 2026-07-27: B-2 seam の導入

### 概要
同期ロジックが `*CC_XXX_ADDR` を直読みしている状態を解消し、`IGameMemory`
インターフェース経由に統一した。差し替え可能になったことで、フェーズ判定を
MBAA を起動せずにテストできる。挙動は変えていない。

### 構成
| 実装 | 配置 | 内容 |
|---|---|---|
| `RealGameMemory` | `core_dll/mbaa_mem/` | 実アドレスへの読み書き。DLL 初期化時に設置 |
| `NullGameMemory` | `core_dll/mbaa_mem/GameMemory.cpp` | 未設置時の既定。読みは 0、書きは捨てる |
| `FakeGameMemory` | `src/tests/` | 値を明示指定し、書き込まれた入力を全フレーム記録 |

インターフェースは読み6 + 書き1。設計書では読み5としていたが、`UIManager` と
`GameFrameOrchestrator` にあった `IsBadReadPtr` ガードの挙動を保存するため
`IsAvailable()` を追加した。

### 移行結果
ライブ経路の `CC_*_ADDR` 直参照はすべてゼロになった。

| ファイル | 直参照 |
|---|---|
| `engine/SceneRunner.cpp` | 0 |
| `engine/MatchScene.cpp` | 0 |
| `engine/FrameControl.hpp` | 0 |
| `engine/GameFrameOrchestrator.cpp` | 0 |
| `mbaa_mem/PhaseMonitor.cpp` | 0 |
| `ui/UIManager.cpp` | 0 |

`SceneFastBoot` には `CC_FORCE_GOTO_ADDR`（コード書換）、`CC_SFX_ARRAY_ADDR`、
`CC_GAME_STATE_ADDR` が残るが、いずれも FastBoot 固有で性質が異なるため
意図的に seam の外に置いている。テストでは FastBoot 自体をスキップする。

`FrameControl` の入力書込みプリミティブ（`GetInputBasePtr` / `WriteP1Input` /
`WriteP2Input` / `LogNullInputBase`）は `RealGameMemory` に移設した。処理内容は同じ。

### 併せて修正した文書の誤り
`GamePhaseDetector.hpp` の `GetIntroState()` doc が「0=イントロ前 / 2=イントロ完了」と
実装と正反対になっていたのを修正。`IsRoundActive()` の説明も
「introState==2 かつタイマー動作中」→「InGame かつ introState==0」に訂正した。
この誤りは `test_phase_monitor.cpp` で固定したため、再発すればテストが赤くなる。

### 検証
単体テスト4スイート132チェック通過。`test_phase_monitor` は `RealGameMemory` を
リンクしていないため、直読みが残っていればクラッシュして露見する構成。

E2E（2窓70秒）を B-1 時点と比較したところ、ログの出現数と最終状態が完全に一致した。

| 項目 | B-1 | B-2 |
|---|---|---|
| `ConfirmRemote MATCH` | 12910 | 12910 |
| `RECV pkt` | 1334 | 1334 |
| `SceneRunner` 定期ログ | 65 | 65 |
| `ConfirmRemote MENU` | 10 | 10 |
| 最終状態 | `phase=2 fip=3840 WT=3885 intro=0 synced=1 alive=1` | 同一 |
| NULL / FAILED / Disconnected | 0 | 0 |

`[InitThread] RealGameMemory installed.` が両プロセスのログ7行目に出ており、
設置が他の初期化より先に行われていることも確認した。

### 変更ファイル
- [NEW] `mbaa_mem/IGameMemory.hpp` — インターフェースと設置口
- [NEW] `mbaa_mem/GameMemory.cpp` — 設置口の実装 + NullGameMemory
- [NEW] `mbaa_mem/RealGameMemory.hpp/.cpp` — 実メモリ実装
- [NEW] `src/tests/fake_game_memory.hpp` — FakeGameMemory + ScopedGameMemory
- [NEW] `src/tests/test_phase_monitor.cpp` — seam 経由の PhaseMonitor テスト（30チェック）
- [MODIFY] `engine/FrameControl.hpp` — `WriteInput` を seam に委譲、プリミティブを撤去
- [MODIFY] `engine/SceneRunner.cpp` / `MatchScene.cpp` / `SceneFastBoot.cpp` — seam 経由に
- [MODIFY] `engine/GameFrameOrchestrator.cpp` / `ui/UIManager.cpp` — `IsBadReadPtr` → `IsAvailable()`
- [MODIFY] `mbaa_mem/PhaseMonitor.cpp` — seam 経由に
- [MODIFY] `mbaa_mem/GamePhaseDetector.hpp` — doc の誤りを訂正
- [MODIFY] `mbaa_mem/dllmain.cpp` — 初期化の最初期に `InstallRealGameMemory()`
- [MODIFY] `src/core_dll/CMakeLists.txt` / `src/tests/CMakeLists.txt` — ソース追加

---

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
