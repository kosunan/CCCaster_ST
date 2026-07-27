# ゲームメモリ seam 設計

対象: `src/core_dll/` / 目的: ゲームを起動せずに同期ロジックを検証可能にする

---

## 背景

凍結の直接原因は個別バグではなく、**仮説を検証する手段が実ゲーム2窓の35秒目視しかなかったこと**。
L1（単体テスト基盤）で依存ゼロのモジュールは押さえたが、症状①②③が住んでいる
「フェーズ遷移・IntroBarrier・Rematch」は全て `*CC_XXX_ADDR` の直読みに縛られており、
プロセス外から再現できない。この seam はその制約を外すためのもの。

---

## 1. seam の範囲（実測）

`SceneRunner::Step()` から毎フレーム到達する経路が触るゲームメモリは以下のみ。

| 種別 | アドレス | 参照元 |
|---|---|---|
| 読 | `CC_GAME_MODE_ADDR` | PhaseMonitor, SceneFastBoot, UIManager, GameFrameOrchestrator |
| 読 | `CC_INTRO_STATE_ADDR` | PhaseMonitor, SceneRunner, MatchScene |
| 読 | `CC_WORLD_TIMER_ADDR` | SceneRunner, MatchScene |
| 読 | `CC_REAL_TIMER_ADDR` | SceneRunner |
| 読 | `CC_MENU_STATE_COUNTER_ADDR` | MatchScene |
| 書 | `CC_PTR_TO_WRITE_INPUT_ADDR` | FrameControl のみ |

### seam に含めないもの

| 対象 | 理由 |
|---|---|
| `DumpEntryList.hpp` (23箇所), `RollbackEngine` | ステート保存用。現在ゲームスレッドからの呼び出し元がない |
| `MbaaPatcher` | 起動時1回のみ。フレームループに関与しない |
| `FastBootRunner` | 廃止済み（CMake から除外済み） |
| `SceneFastBoot` の `CC_SFX_ARRAY_ADDR` / `CC_FORCE_GOTO_ADDR` | コード書換・SFX配列で性質が異なる。テストでは FastBoot 自体をスキップする |
| `GameLauncher.cpp` | 別プロセス（launcher.exe） |

---

## 2. 入力の型付け（最優先）

seam より先にこれを片付ける。現在、入力の符号化が3種類混在している。

| # | 符号化 | 使用箇所 |
|---|---|---|
| 1 | `direction << 16 \| buttons`（direction はテンキー表記 0,1-9） | DirectInputHook, FrameControl, SceneFastBoot ← **実際の規約** |
| 2 | `BIT_UP=0x01 / BIT_DOWN=0x02` のビットマスク | MbaaInputDefs.hpp の定義。MatchScene の Rematch がこの前提で書かれている |
| 3 | `COMBINE_INPUT(dir, btn)` = `dir \| btn << 8` | MbaaInputDefs.hpp。**使用箇所ゼロの死んだマクロ** |

### 既知の実害

`MatchScene::HandleAutoNavigation()` は #2 のつもりで `0x0002`(下) / `0x0001`(上) を返すが、
呼び出し側がシフトせず `GC::WriteInput()` に渡すため #1 として解釈される。

- 下 → direction=0(ニュートラル), buttons=`0x0002` = `CC_PLAYER_FACING`
- 上 → direction=0, buttons=`0x0001` = `CC_BUTTON_START`
- 決定 (`CC_BUTTON_A|CC_BUTTON_CONFIRM` = `0x0410`) のみ正しく動く

結果、Rematch の自動ナビはカーソルを動かせず、その場の項目を確定する。
両者が別の項目を選ぶため画面がずれる。

### 対策

生の `uint32_t` を廃し、型で取り違えを防ぐ。

```cpp
// core_dll/mbaa_mem/GameInput.hpp
namespace cccaster::game_interface {

/// ゲームに書き込む1プレイヤー分の入力。
/// direction はテンキー表記 (0=ニュートラル, 1-9)。buttons は CC_BUTTON_* の OR。
/// ビット位置の取り違えを型で防ぐため、生の uint32_t は境界の外に出さない。
struct GameInput {
    uint16_t direction = 0;
    uint16_t buttons   = 0;

    /// 通信・バッファ用の 32bit 表現（direction << 16 | buttons）
    constexpr uint32_t Pack() const {
        return (static_cast<uint32_t>(direction) << 16) | buttons;
    }
    static constexpr GameInput Unpack(uint32_t v) {
        return { static_cast<uint16_t>((v >> 16) & 0xFFFF),
                 static_cast<uint16_t>(v & 0xFFFF) };
    }
};

} // namespace cccaster::game_interface
```

`Pack()` / `Unpack()` は純関数なので L1 テストで固定できる。
不使用の `COMBINE_INPUT` / `RETURN_MASH_INPUT` / `BIT_*` は削除する
（残すと4つ目の符号化として復活する）。

---

## 3. インターフェース

```cpp
// core_dll/mbaa_mem/IGameMemory.hpp
namespace cccaster::game_interface {

class IGameMemory {
public:
    virtual ~IGameMemory() = default;

    // ── 読み取り（毎フレーム）──
    virtual uint32_t GameMode()         const = 0;
    virtual uint8_t  IntroState()       const = 0;
    virtual uint32_t WorldTimer()       const = 0;
    virtual uint32_t RealTimer()        const = 0;
    virtual uint32_t MenuStateCounter() const = 0;

    // ── 書き込み ──
    virtual void WriteInput(GameInput p1, GameInput p2) = 0;
};

/// プロセス起動時に1回だけ差し込む。以降 GameMem() で参照する。
void         InstallGameMemory(IGameMemory* impl);
IGameMemory& GameMem();

} // namespace cccaster::game_interface
```

実装は2つだけ。

| 実装 | 配置 | 内容 |
|---|---|---|
| `RealGameMemory` | `core_dll/mbaa_mem/` | 現在の `*CC_XXX_ADDR` 直読み・直書きをそのまま移設 |
| `FakeGameMemory` | `src/tests/` | 値を明示的に設定でき、書き込まれた入力列を記録する |

### 呼び出しコストについて

仮想呼び出しは1回あたり約 1-2ns。毎フレーム10回呼んでも 20ns、
1フレーム 16,666,000ns に対して 0.0001%。

本プロジェクトの「妥協なきチューニング」は**無駄なコピーとロック待機**を禁じたもので、
ここは抵触しないと判断する。関数ポインタ表（POD struct）でも同じコストで書けるので、
仮想関数を避けたい場合はそちらでよい — 設計上の差は呼び出し側に出ない。

---

## 4. 2セッションの同居について

決定性テスト（両者がゲームに書く入力列の一致を検証する）には、独立した2セッションが要る。

`SceneRunner` / `MatchScene` は file-scope static、入力バッファはシングルトンなので、
**同一プロセス内に2セッションは立てられない**。

### 採用: 2プロセス方式

脱シングルトン化は行わず、**ヘッドレスハーネス exe を2つ起動**する。

```
harness.exe (Host)  ←── loopback UDP ──→  harness.exe (Client)
  SceneRunner                                SceneRunner
  MatchScene                                 MatchScene
  NetplaySession（本物）                      NetplaySession（本物）
  FakeGameMemory                             FakeGameMemory
```

- シングルトンはプロセスごとに1つなので、現在のコードのまま動く
- 通信は本物の UDP。既存の遅延・ロス注入がそのまま使える
- `FakeGameMemory` がロード時間・intro 遷移をスクリプトで再現する
  （片側だけロードを3秒遅らせる、といった操作が可能になる）
- 両プロセスが書き込んだ入力列をログに出し、突き合わせて一致を判定

`dual_test.bat` と同じ形だが MBAA が要らないため、起動が数秒で済む。

### 却下: 脱シングルトン化

前回の凍結は大規模リファクタの最中に起きている。同じ規模の変更を検証手段が無い状態で
始めるのは同じ失敗の再演になる。ハーネスが立って初めて、脱シングルトン化の是非を
テストで確かめられる。必要になった時点で判断する。

---

## 5. パイプライン再構築時の必須修正: peerFrame のフェーズ跨ぎ汚染

2026-07-27 の `dual_test.bat` 実行（3月11日ビルド）で、報告された
「対戦開始で画面が止まり、音楽だけ鳴り、裏で高速動作する」症状の原因を特定した。

### 因果連鎖（ログで確認済み）

1. CharaSelect 中、MENU 用フレームカウンタが 12551 まで進む
2. `SyncCodec::_latestPeerFrame` は受信 `baseFrame` の**単調最大値**。
   `Reset()` は `Initialize()` からしか呼ばれず、フェーズ遷移では戻らない
3. InGame 突入で `MatchInputBuffer` は 0 にリセットされ、
   MATCH パケットの `baseFrame` も 0 から再スタートする（ログ: `baseFr=0`, `baseFr=7`）
4. だが `GetLatestPeerFrame()` は 12551 を返し続ける
5. `SceneRunner`: `gap = peerFrame - localWriteHead` = `12551 - 0` = 12551
6. `skipWait = (gap >= 2)` → メトロノーム待機をスキップ → 無制限に回る
7. `SetRenderSkipByGap(12551)` → `RenderSkip = ON`
   → `GameFrameOrchestrator::OnPresentSkip()` が `Present()` ごとスキップ → 画面停止
8. 音声は D3D と無関係なので鳴り続ける
9. `whMatch` が 12551 に追いつくまで継続（ログ末尾は `whMatch=12360` で解除直前）

証拠: `peerF=12551` が InGame 最初のログ行から最後の行まで一定。

### 現状

CB撤去により `SceneRunner` の gap 計算が消え `SetRenderSkipByGap(0)` 固定になっているため、
**この症状は現在のツリーでは出ない**。しかし原因は `SyncCodec` に残っており、
入力パイプラインを再構築して gap 制御を戻した瞬間に再発する。

### 本質

MENU と MATCH は**独立したフレーム空間**（それぞれ独立にリセットされる）なのに、
`_latestPeerFrame` という単一の単調カウンタで最大値を取っている。
これは §4 で挙げた「パケットが受信側の内部データ構造を指名している
（`FLAG_BUFFER_MENU` / `FLAG_BUFFER_MATCH`）」問題と同じ根を持つ。

### 対処方針

フェーズ跨ぎで単調増加する値を作らない。以下のいずれかを B-4 までに決める。

- フレーム番号をフェーズごとにリセットせず、セッション通しの単一の番号にする（推奨）
- または `latestPeerFrame` をバッファ種別ごとに分け、フェーズ遷移でリセットする

決定性テスト（B-4）は、フェーズ遷移直後の gap が 0 付近であることを検証項目に含める。

---

## 6. 必須修正: IntroBarrier が事前通知で無効化される

ハーネス(B-3)で `--HostLoadingFrames 60 --ClientLoadingFrames 240` を実行し、
証言②「対戦開始のロードにばらつきがあり制御できなくてずれる」を再現した。

| | ロード | InGame 到達 | IntroBarrier 解除 |
|---|---|---|---|
| HOST | 60F | frame 300 | WT=302 |
| CLIENT | 240F | frame 480 | WT=482 |

HOST は CLIENT の到達を待たず 180 フレーム先行し、そのままずれ続けた。

### 原因

`MatchScene::OnLoading` が Loading 突入時点で `localPhaseReady = true` を立てる。

```cpp
// IntroBarrier 事前通知: Loading 中に localPhaseReady=true を設定し
// GAME_TICK に乗せて peer に通知。InGame 到達時にはバリア待機ゼロを実現。
```

一方 `HandleRoundStartSync` は `peerPhaseReady` を「相手が intro=2 に到達した」
という意味で待っている。ロード時間が左右で違うと、まだ Loading 中の相手からの
事前通知でバリアが解除される。

### 本質

`localPhaseReady` が「Loading に入った」と「intro=2 に到達した」の2つの意味を
兼ねている。これは §5 の `_latestPeerFrame` が MENU と MATCH の2つのフレーム空間を
兼ねている問題と同型で、**1つの変数に2つの意味を持たせたことによる破綻**。

### 対処方針

- 「到達」の通知を意味ごとに分ける（Loading 到達と intro=2 到達を別フラグにする）
- あわせて §5 の transitionId（世代番号）を導入し、どの遷移に対する通知かを区別する
  — 証言③「ワンスアゲインでずれる」はバリアが世代を持たないことが疑われるため

修正後は同じハーネス実行で HOST/CLIENT の IntroBarrier 解除が揃うことを確認する。

---

## 7. 実機テストのゲート

harness は MBAA を起動しないため、フック・DirectInput・描画・ゲーム側の入力
上書きは検証できない。各段階の区切りで実機を1回通すこと。

```powershell
.\src\harness\run_real_pair.ps1                      # 素の疎通
.\src\harness\run_real_pair.ps1 -SimDelay '50,90' -SimLoss 20   # 劣悪回線
```

`dual_test.bat` との違いは3点。

1. **必ずデプロイしてから起動する。** `dual_test.bat` はデプロイしないため、
   ビルドしただけで実行すると古い DLL がテストされ、しかも警告が出ない。
   実際にこの罠を2回踏んでいる（2026-07-27 に B-2 版と 3/11 版で各1回）。
2. **入力を自動生成する。** `CCCASTER_SCRIPT_INPUT=1` で `ScriptedInput()` に
   切り替わる。人が操作すると両者の入力を再現できず決定性を判定できない。
   入力列はフレーム番号だけから決まる純関数で、harness と同じものを使う。
3. **決定性まで判定する。** 両プロセスの `[REC]` ログを突き合わせ、
   同じネットプレイフレームに同じ入力が配られたかを見る。

### 実機でしか出ない不具合の見分け方
| 症状 | 見るところ |
|---|---|
| 配信記録が 0 件 | 入力がゲームに届いていない。`MbaaPatcher` の NOP パッチとフック初期化 |
| 記録はあるが操作が効かない | ゲーム側が入力を上書きしている。書込みタイミング |
| harness では OK で実機のみ不一致 | フック経由の入力取得か、実フレーム進行との噛み合わせ |

---

## 8. 段取り

| # | 内容 | 検証方法 |
|---|---|---|
| B-1 | `GameInput` 型の導入、`FrameControl::WriteInput` を型経由に変更、死んだマクロ削除 | `Pack`/`Unpack` の L1 テスト + `dual_test.bat` で退行なし |
| B-2 | `IGameMemory` 導入、`RealGameMemory` で既存動作を維持 | `dual_test.bat` で退行なし（この時点では挙動不変が成功条件） |
| B-3 | `FakeGameMemory` + `harness.exe` | ハーネス単体が起動し、フェーズ遷移を通過する |
| B-4 | 決定性テスト（2プロセス・入力列突き合わせ） | ①②③の再現と修正 |

B-1 で Rematch のバグが直るが、**実ゲームで確認できるのは B-3 以降**。
それまでは「型として正しくなった」以上の主張はしない。
