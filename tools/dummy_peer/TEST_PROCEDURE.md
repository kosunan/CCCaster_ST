# DummyPeer テスト作業手順書 (AI向け)

## 概要

`DummyPeer` は、本体DLL (`cccaster_hook.dll`) の通信・同期コードを1台のPCでテストするためのスタンドアロン擬似クライアントです。  
ランダム遅延・パケットロス・時計ズレを注入しながら、SYNC_REQ/RESプロトコルとコントローラ入力パケットを模倣します。

---

## ⚠️ 環境前提条件 (必ず読むこと)

### CCCaster_v10.exe の動作要件

`CCCaster_v10.exe` はスタンドアロンアプリではなく **MBAA.exe への DLL インジェクター** です。

```
CCCaster_v10.exe
  └─ SessionNegotiator (相手との接続確立)
  └─ GameLauncher   (MBAA.exe 起動 + DLL 注入)
       └─ cccaster_hook.dll (インゲーム通信・同期処理)
```

**MBAA.exe なしでは negotiation 接続確立まで動作するが、ゲーム起動以降は動作しない。**

### デプロイ先フォルダ構成

```
I:\work_space\CCCaster_v10\_TEST_MBAACC\     ← テスト環境ルート
├── MBAA.exe                                  ← ゲーム本体 (必須)
├── cccaster\                                 ← デプロイ先
│   ├── CCCaster_v10.exe                      ← ビルド済みEXE (要コピー)
│   ├── cccaster_v10.ini                      ← 設定ファイル
│   └── libcccaster_hook.dll                  ← DLL本体 (要コピー)
├── cccaster_hook.dll                         ← (旧フォルダ直下、参照用)
└── cccaster_v10.ini                          ← (旧設定ファイル、参照用)
```

### ビルド済みファイルのデプロイ手順

```powershell
# CCCaster_v10.exe と hook DLL をテスト環境にコピー
# (ビルド後、必ずこの手順を実施してから起動すること)
Set-Location I:\work_space\CCCaster_v10

# EXE コピー
Copy-Item build\bin\CCCaster_v10.exe `
    _TEST_MBAACC\cccaster\CCCaster_v10.exe -Force

# DLL コピー (lib接頭辞付きファイル名に注意)
Copy-Item build\bin\cccaster_hook.dll `
    _TEST_MBAACC\cccaster\libcccaster_hook.dll -Force

# DummyPeer ビルド確認
cmake --build tools\dummy_peer\build -j8
```

### CCCaster_v10.exe の実行フォルダ

**必ず `_TEST_MBAACC\cccaster\` をカレントディレクトリにして実行すること。**  
`../MBAA.exe` の相対パスを正しく解決するため。

```powershell
# 正しい起動方法
Set-Location I:\work_space\CCCaster_v10\_TEST_MBAACC\cccaster
.\CCCaster_v10.exe --headless --host --port 7500
```

---

## ビルド手順

```powershell
# CCCaster 本体ビルド
Set-Location I:\work_space\CCCaster_v10
cmake --build build -j8

# DummyPeer ビルド
$env:PATH = "C:\msys64\mingw32\bin;C:\msys64\usr\bin;" + $env:PATH
cmake -B tools/dummy_peer/build -S tools/dummy_peer -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build tools/dummy_peer/build -j8
```

---

## テストシナリオ

### シナリオ N1: Negotiation 接続確立テスト

CCCaster_v10.exe (Host) ← DummyPeer.exe (Client) の接続確立テスト。  
**ゲーム起動なし。Negotiation のみ確認する最初のテスト。**

> **⚠️ ポート注意**: DLL は `cccaster_v10.ini` の設定にしたがいポート **10800** にバインドする。  
> `--port 7500` などを指定しても DLL インジェクション後のポートは INI の値が優先される。  
> DummyPeer の `--remote-port` は必ず **10800** に合わせること。

```powershell
# Terminal 1: テスト環境から CCCaster_v10.exe を起動（ポート10800）
Set-Location I:\work_space\CCCaster_v10\_TEST_MBAACC\cccaster
.\CCCaster_v10.exe --headless --host --port 10800

# Terminal 2: DummyPeer を Client として接続
Set-Location I:\work_space\CCCaster_v10
.\tools\dummy_peer\build\DummyPeer.exe `
    --mode client `
    --test-mode negotiation `
    --target-ip 127.0.0.1 `
    --local-port 10801 `
    --remote-port 10800 `
    --duration 15
```

**判定基準**: CCCaster 側の出力に以下が表示されること。

```
[ SUCCESS ] Connection established with [127.0.0.1:10801]
[ HEADLESS ] Connection established successfully. Booting game...
```

> **注**: `Booting game...` 後に MBAA.exe が自動起動する。ゲームウィンドウが表示されたら正常。

---

### シナリオ S1: 基本時刻同期テスト (DLL 動作中)

MBAA.exe が起動・DLL 注入された後、DummyPeer が sync テストを実施する。

```powershell
# Terminal 1: CCCaster 起動 (ゲームが立ち上がる)
Set-Location I:\work_space\CCCaster_v10\_TEST_MBAACC\cccaster
.\CCCaster_v10.exe --headless --host --port 7500

# Terminal 2: DummyPeer sync テスト (ゲーム起動後に実行)
Set-Location I:\work_space\CCCaster_v10
.\tools\dummy_peer\build\DummyPeer.exe `
    --mode client `
    --test-mode sync `
    --target-ip 127.0.0.1 `
    --local-port 7501 `
    --remote-port 7500 `
    --preset lan `
    --duration 30
```

**判定基準**: DummyPeer 側のレポートで `syncCompleted: true` が表示されること。

---

### シナリオ G1: 入力パケット結合テスト

DLL への GAME_INPUT パケット送受信テスト（対戦中プロセスへの接続）。

```powershell
# Terminal 1: CCCaster 起動 (ゲームを対戦画面まで進める)
Set-Location I:\work_space\CCCaster_v10\_TEST_MBAACC\cccaster
.\CCCaster_v10.exe --headless --host --port 7500

# Terminal 2: game モードで入力パケット送信
Set-Location I:\work_space\CCCaster_v10
.\tools\dummy_peer\build\DummyPeer.exe `
    --mode client `
    --test-mode game `
    --target-ip 127.0.0.1 `
    --local-port 7501 `
    --remote-port 7500 `
    --preset lan `
    --duration 60
```

**判定基準**: `cccaster_hook_log.txt` に入力フレームの受信ログが出力されること。

---

## テストプリセット

| プリセット | 遅延 | ロス | スパイク | クロックオフセット | ドリフト |
|---|---|---|---|---|---|
| `lan` | 1-3ms | 0% | なし | 50ms | 5ppm |
| `wifi` | 10-40ms | 2% | 100ms@5% | 150ms | 20ppm |
| `4g` | 30-80ms | 5% | 300ms@10% | 200ms | 30ppm |
| `hell` | 50-200ms | 15% | 500ms@20% | 300ms | 50ppm |

```powershell
# プリセット例
.\tools\dummy_peer\build\DummyPeer.exe --test-mode sync --preset wifi --duration 15
```

---

## CLI リファレンス (DummyPeer)

| オプション | デフォルト | 説明 |
|---|---|---|
| `--mode` | `host` | `host` or `client` |
| `--test-mode` | `sync` | `negotiation`, `sync`, `full`, `game`, `e2e` |
| `--preset` | なし | `lan`, `wifi`, `4g`, `hell` |
| `--target-ip` | `127.0.0.1` | 接続先 IP |
| `--local-port` | `10800` | DummyPeer バインドポート |
| `--remote-port` | `10801` | 接続先ポート (CCCaster の `--port` と一致させる) |
| `--min-delay` | `30` (ms) | 最小遅延 |
| `--max-delay` | `60` (ms) | 最大遅延 |
| `--loss` | `0.0` | パケットロス率 0.0〜1.0 |
| `--clock-offset` | `150000` (us) | クロックオフセット |
| `--drift-ppm` | `20` | クロックドリフト率 |
| `--duration` | `10` (sec) | テスト時間 |
| `--output-json` | なし | JSON 結果ファイル出力先 |
| `--assert-max-loss` | 無効 | ロスレート上限アサーション |
| `--assert-no-gaps` | false | フレーム抜けゼロアサーション |
| `--round-frames` | `3600` | E2E: 1ラウンドのフレーム数 |
| `--max-rounds` | `2` | E2E: ラウンド数 |
| `--rematch-choice` | `0` | E2E: 0=再戦, 1=キャラセレ戻り |

---

## CLI リファレンス (CCCaster_v10.exe)

| オプション | 説明 |
|---|---|
| `--headless` | ヘッドレスモード（キー入力待ちスキップ・自動接続） |
| `--host` | Host として待受 |
| `--ip <addr>` | 接続先 IP (Client モード) |
| `--port <n>` | ポート番号 |
| `--hash <str>` | 接続ハッシュ (CLI に貼り付けて接続する場合) |

---

## テストログの確認場所

| ログ | パス | 内容 |
|---|---|---|
| CCCaster 標準出力 | コンソール出力 | 接続状態・FastBoot 進捗 |
| DLL フック詳細 | `_TEST_MBAACC\cccaster_hook_log.txt` | 同期処理・入力受信ログ |
| CCCaster 出力リダイレクト | `_TEST_MBAACC\cccaster_out.txt` | 過去セッションのログ |
| DummyPeer JSON | `--output-json` で指定 | 統計レポート |

---

## ⚠️ よくある失敗パターンと対処

| 症状 | 原因 | 対処 |
|---|---|---|
| CCCaster が起動しない | `cccaster\` 以外から実行した | `_TEST_MBAACC\cccaster\` から実行 |
| `MBAA.exe not found` エラー | デプロイパスが違う | `Set-Location` で `cccaster\` フォルダを確認 |
| DummyPeer に反応がない | CCCaster が negotiation 待ちになっていない | CCCaster を先に起動し `Waiting on Port` が表示されてから DummyPeer を起動 |
| FastBoot が止まらない | ゲームのメニュー状態が変わった | `_TEST_MBAACC\boot_timing.txt` の遷移タイミングを確認 |
