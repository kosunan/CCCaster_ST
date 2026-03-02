# feat: SleepFrame にワールドタイマー同期ゲートを追加

## 2026-03-03: CC_WORLD_TIMER_ADDR vs currentFrame フレーム同期制御

### 背景
SyncCoordinator::currentFrame（通信スレッド駆動）とゲーム内蔵の CC_WORLD_TIMER_ADDR が
独立してカウントアップしており、両者のずれを検出・制御する仕組みがなかった。

### 変更内容
- [MODIFY] `GameControl.hpp`: `SleepFrame()` の currentFrame ポーリング後に
  ワールドタイマーゲートを追加。
  - `worldTimer > syncFrame`（ゲーム先行）→ スピンウェイトで currentFrame の追いつきを待機
  - `worldTimer ≤ syncFrame`（早回し中）→ 何もしない（早回し許容）
  - 高速早回し中のため Sleep なしスピンウェイト

---



## 2026-03-02: Sleep(1)ポーリング廃止 → 精密サブティック方式

### 背景
旧ループは `Sleep(1)` (~1-15ms不定) でポーリングし、200ms間隔でPING/READY送信。
ティック精度が低く、ハンドシェイク応答も最大200ms遅延。

### 変更内容
- [MODIFY] `SyncCoordinator.hpp` — `PING_INTERVAL_US`/`READY_INTERVAL_US` 削除、`SUB_TICKS_PER_FRAME=3` 追加、`SleepUntil()` 宣言追加、`_lastPingSentUs`/`_lastReadySentUs` フィールド削除。
- [MODIFY] `SyncCoordinator.cpp`:
  - `SleepUntil()`: Sleep+スピンのハイブリッド精密スリープ（2ms以上→Sleep(1)、未満→YieldProcessor）
  - `ThreadMain()`: 1F(≈16666μs) を3分割 → ~5555μs間隔のサブティックループ
  - WaitReady/WaitStart: 毎サブティック (~5.5ms) でPING/READY送信（旧200ms→35倍高速化）
  - Counting: `subTickIdx==0` で1Fに1回フレーム進行、サブティック毎にドレイン処理
  - `nextSubTickUs` 累積方式でドリフト補正と連動

---



## 2026-03-02: ドリフト補正の非線形θ偏差ベース刷新 + タイムアウト短縮

### 背景
旧ドリフト補正は `driftRate × BASE_TICK_US` で ~0.03μs/F の補正しか効かず実質無効。
また切断タイムアウト10秒が長すぎ、相手の強制切断時のレスポンスが悪い。

### 変更内容
- [MODIFY] `NetplayClock.hpp` — `_baseThetaUs`（初期基準θ）と `GetThetaDeltaUs()` を追加。θ絶対値ではなく偏差で補正。
- [MODIFY] `NetplayClock.cpp` — GetTickUs() を4段階非線形θ偏差補正に刷新:
  - |Δ| ≤ 500μs: デッドバンド（補正なし）
  - 500μs < |Δ| ≤ 1F(16666μs): 三乗カーブ（攻撃的非線形、最大2666μs）
  - 1F < |Δ| ≤ 2.2F(36665μs): 最大補正（2666μs = 16%速度差）
  - |Δ| > 2.2F: フレームスキップ相当（±16666μs = 1F丸ごと）
- [MODIFY] `SyncCoordinator.hpp` — DISCONNECT_TIMEOUT_FRAMES を 600F(10秒) → 180F(3秒) に変更。

---



## 2026-03-02: ネゴシエーション時UDPポートのDLL側再利用 + レースコンディション修正

### 背景
SyncCoordinatorの同期テストにおいて、VM→ホスト方向のUDP通信が不到達であり、
ホスト側SyncCoordinatorが `WaitReady` から遷移できない問題が発生していた。

### 根本原因1: ポート不一致（VM→ホスト不到達）
- CLI Launcher のネゴシエーション時にOS割当されたエフェメラルポートが `state.localPort` に記録される
- しかし `dllmain.cpp` でクライアント側が `localPort=0` にハードコードされ、DLL側で別のエフェメラルポートが割り当てられていた
- VM側の `peerPort` はネゴ時のポートを指すため、DLLの新ポートに到達しない

### 根本原因2: レースコンディション（WaitReady永久停滞）
- VM側でホストの最初のREADYパケットがSyncCoordinator起動前に到着→ドロップ
- ホスト側がWaitStartに遷移後、PINGのみ送信しREADYを停止
- VM側SyncCoordinatorは永久にWaitReadyに留まる

### 変更内容
- [MODIFY] `dllmain.cpp` — `ctx.localPort = ctx.isHost ? state.localPort : 0` を `ctx.localPort = state.localPort` に変更。HOST/CLIENT共にネゴシエーション時と同じポートを再利用。
- [MODIFY] `SyncCoordinator.cpp` — WaitStartフェーズでもREADYパケットを200ms毎に継続送信するよう変更。遅延起動した相手SyncCoordinatorに対応。

### テスト結果
- 両側でWaitReady→WaitStart→Counting遷移に成功
- θ推定安定（drift≈0）、alive=1（疎通維持）
- IPC syncCompleted flag正常セット
- F=900+ まで安定フレームカウント確認

---

# refactor: 同期エンジン再設計 — NetplayClock + SyncCoordinator 3モード化

## 2026-03-02: TimeSynchronizer/VirtualClock を廃止し NetplayClock に統合

### 設計
- **NetplayClock** (新規): 純粋関数群 — θ推定、ドリフトレート学習(EMA)、1F周期算出、スタート時刻管理
- **SyncCoordinator** (改修): 3モード・ステートマシン — WaitReady→WaitStart→Counting
- **WasapiClock** (変更なし): WASAPI/QPC時計基盤

### 新規ファイル
- `adapter_netplay/timer/NetplayClock.hpp/.cpp` — 同期補正計算エンジン (~150行)

### 改修ファイル
- `SyncCoordinator.hpp/.cpp` — SyncMode列挙、READY(0x15)/START(0x16)パケット、NetplayClock統合、600Fベース疎通タイムアウト

### 削除ファイル (1,399行)
- `TimeSynchronizer.hpp/.cpp` (753行) — θ推定はNetplayClockに統合、3パケットハンドシェイク/Phase1/Phase2は廃止
- `VirtualClock.hpp/.cpp` (646行) — 全機能が呼び出し元なし

### 外部参照修正 (9箇所)
- PacketRouter.cpp/hpp, IpcData.hpp, SceneRunner.cpp, SceneLoading.hpp, SceneCharaSelect.cpp, State_Ui_Logic.hpp, NetplayManager.hpp, MainController.cpp

---



## 2026-03-02: 50F→120F、完了後NormalSpeed（描画ON）

### 変更内容
- `SceneCharaSelect.cpp`: FAST_BOOT_SKIP_FRAMES = 120 定数追加。50F→120Fに変更。
  120F完了後は Pause ではなく NormalSpeed に遷移（RenderSkip=false→描画ON）。

---

# refactor: ゲームメモリへのポーズフラグ書き込みを廃止

## 2026-03-02: CC_PAUSE_FLAG_ADDR 書き込みを全削除
通信スレッドの currentFrame が進まないことで一時停止が実現されるため、
ゲームメモリへの直接ポーズフラグ書き込み（CC_PAUSE_FLAG_ADDR）は不要。

### 変更内容
- `MbaaSpeedController.hpp`: SetGamePause()/SetGameResume() を完全削除。MaintainState() からポーズフラグ維持ロジック削除。
- `GameControl.hpp`: 未使用の SetPauseFlag() を削除。

---

# refactor: DxHookを純粋フック基盤に分離

## 2026-03-02: ImGui管理等をGameFrameOrchestratorに移動

### 設計原則
- DxHook.cpp: D3D9 EndScene/Present/Reset のフック設定＋コールバック呼出のみ
- GameFrameOrchestrator.cpp: ビジネスロジック（DLLロジック実行・ImGui描画・高速スキップ）

### 変更内容
- `DxHook.hpp`: コールバック型を `DeviceCallback(LPDIRECT3DDEVICE9)` に変更。ImGui関連メンバ削除。EndScene/Present/PresentSkip/PreReset/PostReset の5コールバック。
- `DxHook.cpp`: ImGui初期化・バックバッファ判定・InputHook初期化・MbaaSpeedController参照を全削除。フック関数はコールバック呼出＋元関数呼出のみ。
- `GameFrameOrchestrator.hpp`: Shutdown()追加。5つのコールバック関数（OnEndScene/OnPresent/OnPresentSkip/OnPreReset/OnPostReset）宣言。
- `GameFrameOrchestrator.cpp`: ImGui遅延初期化・バックバッファ判定・RenderSkipチェック・UIManager描画・Reset管理をDxHookから移動。
- `dllmain.cpp`: GameFrameOrchestrator::Shutdown()をDxHook::Shutdown()の前に追加。

---

# refactor: 高速モードをRenderSkip+TickBypassの2軸制御に刷新

## 2026-03-02: Sleep完全廃止 + DxHook描画スキップ + 通信スレッド周期バイパス

### 設計原則
- 高速モード: DxHook EndScene描画スキップ（RenderSkip=true） + 通信スレッド1F周期を待たない（TickBypass=true）
- 通常モード: SyncCoordinator の currentFrame 変化をポーリングして待機
- Sleep(0)/Sleep(1) のような無条件スリープは完全廃止

### 変更内容
- `MbaaSpeedController.hpp`: `RenderSkip()` / `TickBypass()` staticアトミックフラグ追加。SetMode() で高速モード時 ON、通常/Pause 時 OFF。
- `DxHook.cpp`: Hooked_EndScene 先頭で RenderSkip チェック → true なら ImGui + 描画をスキップして元 EndScene を即呼出。
- `GameControl.hpp`: SleepFrame() 内の Sleep(1) を削除。高速モード → 即リターン。通常モード → SyncCoordinator::GetState().currentFrame ポーリング（Sleep+Spin ハイブリッド）。

---

# refactor: TimeSynchronizer/VirtualClock参照を通信スレッドに限定（Phase 6）

## 2026-03-01: DLLスレッドからの旧コンポーネント参照を全削除
SyncCoordinator::GetState() の Read-only 参照に統一。

### 変更内容
- `GameControl.hpp`: IsSynced/IsSyncFailed/UpdateSync/ApplySyncOffset/ResetSync/GetRttUs/GetClockOffsetUs/UpdateLayer1FrameDuration を全削除。SleepFrameをSleep(1)に簡略化。Clock()/Sync()アクセサ削除。
- `MbaaSpeedController.hpp`: VirtualClock include・SetSkipMode() 呼び出し削除。GetCurrentMode() 追加。
- `SceneRunner.cpp`: VirtualClock::Initialize/GC::ResetSync/GC::UpdateSync の呼び出し全削除。旧GC::IsSyncedフォールバック削除。
- `SceneInGame.cpp`: TimeSynchronizer/VirtualClock include 削除。HandleRoundStartSync・ReadAndSend のGC同期メソッド→SyncCoordinator::GetState()。ApplyRelativeCorrection→no-op。フレーム計測動的調整→削除。VClock::QPCNowUs→ローカルQPCNowUs。
- `SceneCharaSelect.cpp`: TimeSynchronizer/VirtualClock include 削除。HandleTimeSyncWait/ReadAndSend→SyncCoordinator::GetState()。UpdateLayer1FrameDuration→削除。
- `SceneLoading.cpp`: TimeSynchronizer include 削除。Phase2同期待ち→SyncCoordinator::GetState()。Phase3ディレイ算出をctx.delayベースに変更。

---

# refactor: 旧同期パスの段階的整理（Phase 5）

## 2026-03-01: SyncCoordinator動作時の旧パス無効化
SyncCoordinator稼働時に旧TimeSynchronizer/VirtualClockの駆動パスをスキップ。

### 変更内容
- `SceneRunner.cpp`:
  - `VirtualClock::Initialize()` → SyncCoordinator未起動時のみ実行
  - `GC::UpdateSync()` ×2箇所 → SyncCoordinator動作時スキップ
  - 旧パスはフォールバックとして残存（完全削除はテスト確認後）

---

# refactor: DLLスレッド SyncCoordinator 統合（Phase 4）

## 2026-03-01: SceneRunner を SyncCoordinator ベースに統合
DLLスレッド（SceneRunner）の同期状態チェックを SyncCoordinator の Read-only 参照に切り替え。

### 変更内容
- `SceneRunner.cpp`:
  - SyncCoordinator.hpp を include
  - 同期完了判定: `syncState.isSynced` を優先、旧 `GC::IsSynced()` はフォールバック
  - 疎通チェック: `syncState.isPeerAlive` を優先、旧10秒タイムアウトも併用
  - 旧 `GC::IsSyncFailed()` チェックを廃止（SyncCoordinator が管理）

---

# refactor: SyncCoordinator 通信スレッドループ実装（Phase 3）

## 2026-03-01: 通信スレッドループ実装
SyncCoordinator の通信スレッドに受信処理・θ推定・疎通チェック・PING送信を実装。

### 変更内容
- `SyncCoordinator.hpp`: ReceivedPacket構造体、OnPacketReceived()、DrainAndProcessPackets()、
  UpdateThetaFromPacket()、SendPacket()、SendPing()、θ推定用リングバッファを追加
- `SyncCoordinator.cpp`: 通信スレッド全処理の実装
  - 受信キューdrain（mutex + vector swap）
  - θ推定: リングバッファ最大値フィルタ + σ安定判定(<1ms → isSynced)
  - 疎通チェック: 10秒タイムアウト → isPeerAlive=false
  - PING送信: 統一ヘッダのみ、200ms間隔
  - パケット送信: NetplayManager::GetUdpSocket()経由
- `PacketRouter.cpp`: OnPacket()先頭にSyncCoordinator.OnPacketReceived()転送を追加

---

# refactor: SyncCoordinator 基盤新設 + WasapiClock 独立抽出

## 2026-03-01: SyncCoordinator 基盤（Phase 2）
通信スレッドがティックマスターとなる新アーキテクチャの基盤を新設。

### 新規ファイル
- `adapter_netplay/SyncCoordinator.hpp/.cpp`
  - SharedSyncState（atomic フィールド群: currentFrame, isSynced, isPeerAlive, remoteInputs[20]）
  - 通信スレッドのティックループスケルトン（WASAPI 16666μs 可変周期）
  - CalcTickDuration(): ディレイ補正（最大18000μs）+ θ補正の2段変動
  - PushLocalInput(): DLLスレッドからのローカル入力送信キュー
- `adapter_netplay/timer/WasapiClock.hpp/.cpp`
  - TimeSynchronizer から WasapiClock クラスを独立ファイルに抽出
  - WASAPI IAudioClock 時刻取得 + QPC フォールバックの統合 GetTimeUs()

### 変更ファイル
- `adapter_netplay/timer/TimeSynchronizer.cpp`
  - 旧 WasapiClock クラス（~140行）を削除、新 WasapiClock.hpp に委譲
- `CMakeLists.txt`
  - WasapiClock.cpp, SyncCoordinator.cpp をビルド対象に追加

---

# refactor: FastBoot をゲームスレッド準拠化（裏スレッド廃止）

## 2026-03-01: FastBoot ゲームスレッド準拠化
裏スレッド（`CreateThread`）で動いていた FastBoot を廃止し、
ゲームスレッド（`SceneRunner::Step()`）で実行する `SceneFastBoot` に置換。

### 変更理由
- 裏スレッドとゲームスレッドのメモリ競合リスクを排除
- フレームスキップを `MbaaSpeedController::HighSpeedSkip_Normal` に統一（`CC_SKIP_FRAMES` 直書き廃止）
- 入力偽造を `GC::WriteInput()` に統一

### 変更ファイル
- [NEW] `SceneFastBoot.hpp/.cpp` — ゲームスレッド上の高速起動 Scene
- [MODIFY] `SceneRunner.cpp` — `phase < CharaSelect` 時に `SceneFastBoot::ProcessFrame()` をディスパッチ
- [MODIFY] `dllmain.cpp` — `FastBootRunner` の include と `Stop()` 呼び出しを削除
- [MODIFY] `CMakeLists.txt` — ソースリスト更新
- [DELETE] `FastBootRunner.hpp/.cpp` — 裏スレッド版を廃止（CMakeから除外、ファイルは残存）

---

# refactor: フレーム処理順序を最適化（入力→送信→Sleep→受信→ゲームロジック）

## 2026-03-01: フレーム処理順序の最適化
全4Sceneの `Update()` を `ReadAndSend()` + `ProcessFrame()` の2フェーズに分割し、
`SceneRunner::Step()` を最適フレーム処理順序に再構成。

### 変更理由
- 入力→送信の間にゼロ遅延で最低レイテンシを実現
- SleepFrameの~16msを受信バッファとして活用
- ゲームロジック実行時点で相手入力の到着確率を最大化

### 変更ファイル
- [MODIFY] `SceneCharaSelect.hpp/.cpp` — `ReadAndSend()` + `ProcessFrame()` に分割
- [MODIFY] `SceneInGame.hpp/.cpp` — `ReadAndSend()` + `ProcessFrame()` に分割
- [MODIFY] `SceneLoading.hpp/.cpp` — `ReadAndSend()` + `ProcessFrame()` に分割
- [MODIFY] `SceneRematch.hpp/.cpp` — `ReadAndSend()` + `ProcessFrame()` に分割
- [MODIFY] `SceneRunner.cpp` — Phase A(ReadAndSend)→SleepFrame→Phase B(ProcessFrame)→状態監視→中断チェック の順に再構成

---

# refactor: FastBoot起動の責務をdllmain.cppからSceneRunner::Init()へ移動

## 2026-03-01: FastBoot 起動責務の移動
- [MODIFY] `SceneRunner.cpp` — `Init()` 末尾 (`s_ready = true` の直前) で `FastBootRunner::Start()` を呼ぶように変更。ゲームロジック層が自らの初期化完了後に FastBoot を起動する設計へ。
- [MODIFY] `dllmain.cpp` — 初期化ステップ(8)の FastBoot 起動コードを削除。`DLL_PROCESS_DETACH` の安全停止 (`FastBootRunner::Stop()`) は維持。

---


## 2026-03-01: GameHooks 責務分割リファクタリング
- [DELETE] `adapter_netplay/GameHooks.hpp`, `GameHooks.cpp` — 5責務混在の巨大クラスを廃止
- [NEW] `game_memory_accessor/MbaaPatcher.hpp/.cpp` — NOP パッチ / キーボードクリア / 非アクティブ判定無効化（起動時1回適用）
- [NEW] `game_memory_accessor/FastBootRunner.hpp/.cpp` — メニュー自動遷移・イントロスキップ（裏スレッドループ）
- [NEW] `adapter_netplay/NetplayManager.hpp/.cpp` — UDP ソケット管理 + SendFunc 提供（GameHooksをリネーム＆絞り込み）
- [MODIFY] `dllmain.cpp` — 初期化シーケンスを MbaaPatcher → TimeHooks → NetplayManager → DxHook → SceneRunner → FastBootRunner の8段階に分離
- [MODIFY] `TimeSynchronizer.cpp` — GameHooks 参照（4箇所）を NetplayManager に置換
- [MODIFY] `CMakeLists.txt` — ソースリスト更新

---


## 2026-03-01: プロジェクトルートのドキュメントと運用環境の最適化
- [ADD] プロジェクトの顔となる `README.md` をルートディレクトリに新設。基本理念やビルド方法への導線を整備。
- [ADD] 開発者間で用語の定義ブレを防ぐためのドメイン用語集 `GLOSSARY.md` を新設（Rollup, CCTR, DummyPeerなど）。
- [ADD] 開発フローやコミットの厳格なルールを定めた参加ガイドライン `CONTRIBUTING.md` を新設。
- [MODIFY] `AI_WORKSPACE_GUIDE.md` が抱え込んでいた責務（概要やコミットルール等）を新設した標準ドキュメントに委譲し、内容をスリム化。
- [CHORE] ルートディレクトリに散乱していた一時ログファイル (`*.log`, `*.txt`) を `build_logs/` 配下へ一括移動し整理。
- [MODIFY] `.gitignore` を更新し、`docs/` および `src/` 配下を除く `*.txt` ファイルがプロジェクトルートにコミット・生成されないように追記。

---


## 2026-03-01: 古い変更履歴の削除・設計資料のアーカイブ化・要件仕様書の同期
- [DELETE] 旧運用ルールに基づいていた `docs/changelogs/` 以下のすべてのディレクトリおよびファイルを削除（全140ファイル）。今後はルートの `changelogs/` で一元管理。
- [MOVE] `docs/design/` 配下に散在していた過去の日付付き意思決定メモや作業方針書 (`2026-*.md`) を `docs/archive/design/` へ一括退避（全27ファイル）。
- [MODIFY] `docs/requirements/` 内に存在していた未登録の仕様書 (`06_packet_specification.md` 〜 `12_packet_field_specification.md`) を精査し、正式な要件であると確認したため、`AI_WORKSPACE_GUIDE.md` の目次ツリー記述を同期して更新。
