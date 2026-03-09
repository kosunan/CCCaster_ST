# DummyPeer 業務テスト品質向上 対策計画書

## 概要
DummyPeerを用いた結合テストにおいて発見された不安要素に対する対策案と実装状況。

---

## 実装済み対策一覧

### ✅ #2: パケット順序逆転 (Out-of-Order) の再現
- `NetworkSimulator` の遅延キューを `std::priority_queue` に変更済み。各パケットの遅延が独立なので逆転が自然に発生する。

### ✅ #5: フレーム番号の連続性チェック
- `FrameSequenceValidator` クラスを実装済み（`TestDiagnostics.hpp`）。抜け・順序逆転を検出。

### ✅ #7: バースト中のフレーム間隔ヒストグラム
- `FrameIntervalHistogram` クラスを実装済み（`TestDiagnostics.hpp`）。<5ms/5-15ms/16-30ms/>30ms バケット。

### ✅ #8: テスト自動判定基準 (Assert)
- `--assert-max-loss` / `--assert-no-gaps` CLIオプション実装済み。exit code 0/1 で判定。

### ✅ (新) DummyPeer.cpp 構造分割
- 896行の巨大ファイルを4つのTestRunnerクラスに分割:
  - `NegotiationTest`: 接続確立テスト
  - `SyncTest`: 時刻同期 + 入力テスト
  - `FullTest`: Phase 1.5→2→4 フルサイクル
  - `GameTest`: CCCaster結合テスト（runSyncAndWait/recvAndHandleSyncをメソッド化）
- `DummyPeer.cpp` は約100行のディスパッチャーに縮小

### ✅ (新) テストプリセット
- `--preset lan|wifi|4g|hell` で一括設定。network simulation パラメータを即座に適用。

| プリセット | 遅延 | ロス | スパイク | 用途 |
|---|---|---|---|---|
| `lan` | 1-3ms | 0% | なし | LAN環境 |
| `wifi` | 10-40ms | 2% | 100ms@5% | 無線LAN |
| `4g` | 30-80ms | 5% | 300ms@10% | モバイル |
| `hell` | 50-200ms | 15% | 500ms@20% | 極限 |

### ✅ (新) JSON構造化レポート (`--output-json`)
- `ReportWriter` クラスで `TestResult` + `Config` をJSON出力。CI/CDパイプライン統合に対応。

### ✅ (新) 切断・再接続引数
- `--disconnect-at <sec>` / `--reconnect-after <sec>` のCLI引数を追加（ランタイム実装は今後）。

---

## 未実装 対策一覧

### 🔴 #1: 双方向同時通信の未テスト
- `SyncResponder` に `InitiateSync()` メソッド追加が必要。

### 🟡 #3: WASAPIクロックの実機異常テスト不可
- DLL側 `VirtualClock` に `InjectTestJump()` テストフック追加が必要。

### 🔴 #4: RollbackEngineとの結合テスト皆無
- 前提条件: RollbackEngine 本体の実装完了。

### 🟡 #6: 切断・再接続のランタイム実装
- CLI引数は追加済み。テストランナー内での `closesocket` → `socket` + `bind` 実装が残っている。

### ⚪ #9: NAT越え・ファイアウォール環境
- ドキュメント追記のみ。DummyPeerの責務外。

### 🟡 #10: テスト時間不足によるドリフト未蓄積
- ロングランテストシナリオの追加とドリフト蓄積比較レポートが必要。

---

## 実装優先順位（残件）

| 優先度 | 対策 | 工数目安 | 前提条件 |
|---|---|---|---|
| 1 | #6 切断・再接続ランタイム | 中 | なし |
| 2 | #1 双方向同時通信 | 中 | なし |
| 3 | #10 ロングランテスト | 小 | なし |
| 4 | #3 WASAPIモック | 小 | DLL側変更 |
| 5 | #4 RollbackEngine結合 | 大 | RollbackEngine完成後 |
| 6 | #9 NAT越え | ドキュメント | 別PC環境 |
