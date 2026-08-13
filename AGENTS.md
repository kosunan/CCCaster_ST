# CCCaster_v10

MBAACC (Ver.1.07 Rev.1.4.0) 用のロールバック通信ツール。DLL をゲームプロセスに注入し、対戦処理をすべてゲーム内で行う。

応答・思考・コミットメッセージ・changelog はすべて日本語で書く。

詳細な根拠は `docs/issues/AUDIT_2026-08-13.md`（全体監査レポート）を参照。以下の記述はすべてそこから来ている。

---

## 現在地（2026-08-13 監査時点）

**入力パイプラインは結線済みで、入力はゲームに届く。** 2026-03 時点の「入力パイプラインを全撤去した状態で停止中」という記述は誤りだった。2026-07 の 2a〜2d で再結線が完了している。

| 2026-03 の記述 | 実装（2026-08-13 確認） |
|---|---|
| `SceneRunner::Step()` の入力取得→書込みは削除済み | `SceneRunner.cpp:168-207` (E) が毎フレーム書込み、`:232-264` (F2) が毎フレーム読出し→`GC::WriteInput` |
| `MatchInputBuffer` 等は呼び出し元がない | `SceneRunner.cpp:171,184,202,237` から常時呼ばれる |
| `SceneInputFilter::Apply()` は no-op | `SceneInputFilter.cpp:51-96` に実装済み。単体テスト16ケースあり |
| 入力は一切ゲームに届かない | **届く** |

真に未接続なのは `rollback/` 一式のみ（`src/core_dll/CMakeLists.txt:45` でビルド対象から除外）。

### 現在の問題は「入力が届かないこと」ではなく「デシンク」

実機・harness ともにデシンクが再現している。根本原因は一文で言える。

> `netFrame` は「`SceneRunner::Step()` が背圧を通過した回数 + 200」でしかなく（`NetplaySession.cpp:56` が原点を 200 に固定）、ゲーム側の時計（WorldTimer）と**一度も突き合わされていない**。背圧が両者の*番号*だけを D+R 以内に強制的に揃えるため、起動タイミング・FastBoot・ロード時間の差で生じたゲーム状態の食い違いが「番号は合っている」という形で隠蔽され、恒久化する。

`NetplaySession.cpp:222` の Counting 突入が唯一の合流点だが、そこで `GetWriteHead()` を読んでも `:226` の DebugLog に渡すだけで再基準化していない。

---

## 落とし穴

### 同期の仕掛けは7つあるが、実際に効いているのは背圧1つだけ

「実装されている＝動いている」と読まないこと。以下はすべて**コードは存在するが機能していない**。

| 機構 | 状態 | 根拠 |
|---|---|---|
| **IntroBarrier** | **何もブロックしない** | `MatchScene.cpp:130` が `HandleRoundStartSync` の bool 戻り値を捨て、`OnInGame` は void。`SceneRunner.cpp:236-264` の (F2) が無条件実行され、しかも (E) の書込みはそれより前に完了済み |
| **phaseBaseFrame** | 恒久 0 | `MatchScene.cpp:103` がリテラル 0 を store → `SyncCodec.cpp:183` の `if(>0)` が**到達不能**。`peerPhaseBaseFrame` は書かれも読まれもしない死フィールド |
| **phaseBaseWorldTimer** | 代入のみ | `MatchScene.cpp:100` で代入、読み手は `:105` のログだけ |
| **WorldTimer / RealTimer / RoundTimer** | 算術利用ゼロ | production コードでの用途はログと `MbaaMemTrace` のみ |
| **`_latestPeerFrame`（相手のフレーム番号）** | ログのみ | `NetplaySession.cpp:273`。ペース制御にも背圧にも入っていない |
| **α1 補正** | 実質無効 | `alpha1 = max(0, RTT/2 - (D+R)*16666)`。D=2,R=4 なら RTT 200ms 未満で常に 0。かつ両者が同じ値を出すので**相対補正にならない** |
| **`SetRenderSkipByGap(gap)`** | 常に 0 | `SceneRunner.cpp:163` が定数 0 を渡す。gap 追従キャッチアップは死んでいる |

### `SetSleepBypass(true)` は自DLL の `Sleep` も殺す

`dllmain.cpp:221-222` の `SetTimeMultiplier(1000)` / `SetSleepBypass(true)` は**恒久設定**で、どこでも解除されない。これは MinHook のインラインフックなので、ゲームだけでなく**自DLL が呼ぶ `Sleep(1)` も `Sleep(0)` に化ける**。結果、通信スレッドとゲームスレッドが常時ビジースピンし、CPU 2コアが張り付く（`TimeHooks.cpp:39-44` / `NetplaySession.cpp:152` / `Metronome.cpp:63`）。待機を入れるときは `TimeHooks::RealSleep()` を使うこと。

### Metronome 起動前はゲームが無制限 FPS でフリーランする

Metronome は Counting 到達まで Start しない（`NetplaySession.cpp:223`）。一方 TimeHooks は上記のとおり Sleep とゲーム内蔵リミッタを既に殺している。この窓（最低0.5秒以上）でゲームは無制限 FPS で走り、`writeHead` が暴走する。背圧もこの時点では無効（ハンドシェイク中は `baseFrame=0` で `SyncCodec.cpp:141` の受信ガードが偽 → `ConfirmRemote` が一度も呼ばれない → `SceneRunner.cpp:185` で `confirmed = head` → lead=0）。

### `RenderSkip` が初期化を丸ごとブロックする

`GameFrameOrchestrator.cpp:116-118,121-145` / `SceneRunner.cpp:111`。FastBoot が完了しないと ImGui / WndProcHook / DirectInput の初期化に到達せず、オーバーレイもコントローラも永久に死ぬ。「オーバーレイが出ない」を UI 側のバグとして追うと外す。

### `MbaaAddresses.hpp` の定数は 8割が未使用

定数 約130個のうち DLL が実際に触るのは約29個（22%）。特に **`CC_P1/P2_CHARA_SELECTOR_ADDR` 等のキャラセレ同期系は全て未使用＝キャラセレ同期は未実装**。定数の存在を実装の根拠にしないこと。

### `SceneRunner.cpp` のセクションラベルが重複している

`(F2)` が `:232` と `:284` に、`(G)` が `:266` と `:296` にある。ラベルで会話・grep すると別の場所を指す。

### 32bit 限定 — ただし CMake は強制していない

`C:\msys64\mingw32\bin` でビルドする。mingw64 でも DLL は問題なく生成されるが、32bit の MBAA.exe に注入できず無言で失敗する。**CMake には 32bit 強制ガードが一切なく**、環境次第で無言で 64bit DLL ができる。ビルド後に必ず bitness を確認すること。

### その他

**メモリアドレスはバージョン専用** — `MbaaAddresses.hpp` の全アドレスは MBAACC Ver.1.07 Rev.1.4.0 のもの。他バージョンでは全滅する。

**`CC_SKIP_FRAMES_ADDR` (0x55D25C) は使用禁止** — 描画スキップはゲームメモリではなく API hook 側で行う。定義自体がコメントアウトしてある。

**`CC_INTRO_STATE_ADDR` の意味がヘッダと実装で正反対** — `GamePhaseDetector.hpp` の doc コメントは「0=イントロ前 / 2=イントロ完了」と書いてあるが**これは誤り**。正しくは `PhaseMonitor.cpp` の実装コメントと `MbaaAddresses.hpp` 側で、**2=イントロ演出中 / 1=pre-game / 0=対戦進行中**（数値が大きいほど手前）。ヘッダを信じると同期条件が丸ごと反転する。

**1つの概念に3つの名前** — ファイル `GamePhaseDetector.hpp` / クラス `PhaseMonitor` / 実装 `PhaseMonitor.cpp`（先頭コメントは `GameMonitor.cpp`）。grep のとりこぼしに注意。

**ゲームスレッドでブロックしてはいけない** — `SceneRunner::Step()` は `DxHook::Hooked_Present` から毎フレーム呼ばれる。ここで相手入力を待ってループすると、描画停止だけでなくキープアライブが不正な状態のまま送られて相手に無視され、`Peer Disconnected` に至る（2026-03-11 に実際に発生）。待ち合わせは必ず「return して次フレームで再チェック」で書く。

**フォルダ名から namespace を推測できない** — 過去のリネームで乖離している。新規ファイルは必ず周辺ファイルに合わせる。

| フォルダ | namespace |
|---|---|
| `engine/` | `cccaster::domain::session` / `cccaster::domain::scene` |
| `sync/` | `cccaster::core::netplay` |
| `rollback/` | `cccaster::sync` |
| `hook/` | `cccaster::core::hooks` / `cccaster::game_interface` |
| `mbaa_mem/` | `cccaster::game_interface` / `cccaster::game_memory` |
| `ui/` | `cccaster::domain::ui` / `cccaster::overlay` |

**docs/ は仕様書ではなく経緯の記録** — `docs/`・`docs/issues/ISSUE_TRACKER.md`・`docs/PROGRESS.md` は 2026-02〜03 当時の構成（`src/domain/`, `domain_netplay/`, `tools/dummy_peer/`）を前提に書かれており、現在のツリーと一致しない。仕様の根拠はコードを読むこと（`docs/issues/AUDIT_2026-08-13.md` は例外で、2026-08-13 のコードのみを根拠にしている）。

**DummyPeer は存在しない** — `tools/` は gitignore 済みかつ実体も消失。docs 中の DummyPeer 手順はすべて実行不能。

**`.agents/` も gitignore 済み** — ワークフロー定義はこのリポジトリをクローンしても付いてこない。

**ルート直下の `*.txt` / `*.log` / `*.exe` は追跡されない**（`.gitignore` で除外、`src/**` と `docs/**` のみ例外）。調査メモをルートに置くと消える。

---

## ビルドとテスト

```powershell
$env:PATH = "C:\msys64\mingw32\bin;C:\msys64\usr\bin;" + $env:PATH
cmake --build build -j12
```

クリーンビルドは asio / MinHook / ImGui を FetchContent で取得するためネット接続が必要。手順の詳細は `.agents/workflows/build.md`。

**32bit でないと configure が落ちる** — 64bit ツールチェインでの構成は `FATAL_ERROR` で止まる。以前は無言で 64bit DLL ができ、注入が失敗する理由が分からないまま時間が溶けた。

**対象 Windows は 7 以上に固定** — `_WIN32_WINNT=0x0601` をルートの CMake で定義している。以前はツールチェイン既定値任せで、MSYS2(GCC15) では通り古い mingw-w64 では `inet_pton` が未宣言になった。

### Linux での検証（実機不要な範囲）

harness と単体テストは **Linux ネイティブで動く**。実機に触れない環境でも同期ロジックの検証が回せる。

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure
./src/harness/run_pair.sh --host-loading 60 --client-loading 180
```

Windows 側を壊していないかの**コンパイル確認**も Linux からできる。生成物は MSYS2 の GCC とバージョンが違うので**実機には使わないこと**。

```bash
cmake -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake
cmake --build build-win32 -j
```

`i686-w64-mingw32` は **posix スレッドモデル**が必須（win32 モデルは `std::thread` / `std::mutex` を提供せず、`NetplaySession` と `LogSink` がビルドできない）。Debian/Ubuntu では `g++-mingw-w64-i686-posix`。

**Linux の harness はクロック品質の検証には使えない** — Windows は WASAPI のオーディオクロック、Linux は `CLOCK_MONOTONIC` で、ドリフト特性が別物。使ってよいのは同期ロジックの決定性検証まで。

**Wine で MBAA.exe を動かす経路は採らない** — 32bit ローダと D3D9 の用意が見合わない。実機検証は Windows で行う。

単体テストは `src/tests/` に置き、`ctest --test-dir build --output-on-failure` で実行する（ゲーム不要・数十ms）。対象が要求する外部シンボルは `stub_*.cpp` で置換する方式。

`[HAZARD]` で始まるテストケースは**現在の危険な挙動をそのまま固定したもの**で、正しさの保証ではない。失敗したらテストを直すのではなく、その挙動を変えたのが意図的かを判断すること。

**フレーム空間を固定するテストは1つも無い** — 「同じ `netFrame` なら同じ WorldTimer / mode / intro」を検証するテストが存在しない。デシンクがテストをすり抜けるのはこのため。

E2E は `_TEST_MBAACC/dual_test.bat`（実ゲーム2窓）。手順は `.agents/workflows/test.md`。ログは各 `MBAACC_N/cccaster/cccaster_hook_log.txt`。

**`dual_test.bat` の遅延・ロス注入は対戦パケットに効いていない** — `NetworkSimulator` が DLL 側で有効化されておらず、`--sim-delay` / `--sim-loss` が作用するのはネゴシエーションパケットだけ（`main.cpp:79` / `UdpSocket.cpp:74-101`）。バッチには「遅延50-90ms・ロス20%」と書いてあるが**対戦中の通信は無劣化**。ロス耐性の試験は一度も行われていない。

**`dual_test.bat` はデプロイしない** — ビルドしただけで実行すると古い DLL がテストされ、しかも何の警告も出ない。`build.bat` を使うか、実行前に `build/bin/` の DLL と `MBAACC_1` / `MBAACC_2` 側のハッシュを比べること。

---

## コミット

コード変更と `changelogs/CHANGELOG_YYYY-MM.md` の追記を**同一コミット**に含め、changelog 先頭の見出しとコミットメッセージを一致させる。
