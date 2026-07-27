# CCCaster_v10

MBAACC (Ver.1.07 Rev.1.4.0) 用のロールバック通信ツール。DLL をゲームプロセスに注入し、対戦処理をすべてゲーム内で行う。

応答・思考・コミットメッセージ・changelog はすべて日本語で書く。

---

## 現在地（2026-03-12 で作業停止中）

最終作業は **入力パイプラインの意図的な全撤去** で、その状態のまま止まっている。

- `SceneRunner::Step()` の入力取得→CB書込みと、相手入力待ち→ゲームメモリ書込みが削除済み
- `MatchScene::OnCharaSelect` / `OnLoading` / `OnInGame` は実質空関数
- `MatchInputBuffer` / `MenuInputBuffer` / `SceneInputFilter` / `RollbackEngine` はファイルとして残るが**ゲームスレッドからの呼び出し元がない**

つまり、接続・時刻同期・IntroBarrier までは動くが**入力は一切ゲームに届かない**。これはバグではなく未実装。上記4クラスは「現行仕様」ではなく「再構築の素材」として読むこと。

---

## 落とし穴

**32bit 限定** — `C:\msys64\mingw32\bin` でビルドする。mingw64 でも DLL は問題なく生成されるが、32bit の MBAA.exe に注入できず無言で失敗する。

**メモリアドレスはバージョン専用** — `MbaaAddresses.hpp` の全アドレスは MBAACC Ver.1.07 Rev.1.4.0 のもの。他バージョンでは全滅する。

**`CC_SKIP_FRAMES_ADDR` (0x55D25C) は使用禁止** — 描画スキップはゲームメモリではなく API hook 側で行う。定義自体がコメントアウトしてある。

**`CC_INTRO_STATE_ADDR` は数値が大きいほど手前** — 2=キャラ紹介中 / 1=pre-game / 0=in-game。ラウンド開始同期は `intro == 2` で待ち合わせる。

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

**docs/ は仕様書ではなく経緯の記録** — `docs/`・`docs/issues/ISSUE_TRACKER.md`・`docs/PROGRESS.md` は 2026-02〜03 当時の構成（`src/domain/`, `domain_netplay/`, `tools/dummy_peer/`）を前提に書かれており、現在のツリーと一致しない。仕様の根拠はコードを読むこと。

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

E2E は `_TEST_MBAACC/dual_test.bat`（実ゲーム2窓・遅延50-90ms・ロス20% 注入）。手順は `.agents/workflows/test.md`。ログは各 `MBAACC_N/cccaster/cccaster_hook_log.txt`。

---

## コミット

コード変更と `changelogs/CHANGELOG_YYYY-MM.md` の追記を**同一コミット**に含め、changelog 先頭の見出しとコミットメッセージを一致させる。
