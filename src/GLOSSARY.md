# CCCaster Steam 用語

現在のコードを読むための用語です。過去資料の固定サイズや旧クラス名を現行の契約として扱わず、定義元を参照してください。

| 用語 | 意味・定義元 |
|---|---|
| D / R | Dは入力遅延、Rは予測・巻戻しの上限。Dは0〜8、既定2。Rは通常7F・一時予測上限20Fの自動管理。[NetplaySettings](src/shared_contracts/NetplaySettings.hpp) |
| Rollback / Rollup | 届いた確定入力と予測の不一致で状態を復元し、ゲームスレッド上で追いつくまで再計算する。中間画像のPresent・通常待機を省略する。利用者向けREP再生とは別 |
| 確定フレーム | 両者の入力が揃い、予測の訂正が済んだ範囲。検証では同じ番号の最終再計算結果を比較する |
| 世代（epoch） | 画面・ラウンド境界で切り替える入力番号空間。古い入力や通知が次の世代へ混入するのを防ぐ。[FrameSequence](src/core_dll/sync/FrameSequence.hpp) |
| InputTimeline | ゲーム更新から独立した時計で入力を採取し、番号と締切を管理する。[InputTimeline](src/core_dll/sync/InputTimeline.hpp) |
| RTT / JIT / 1F | RTTは通信の往復時間、JITは新しいRTTサンプルの隣接差による揺らぎ、1Fはゲーム更新の実測間隔。物理コントローラや表示パネルの遅延実測ではない |
| WASAPI / QPC | 音声時計と高分解能OS時計。WASAPIストリームを維持し、故障時はQPCへ固定切替する。両PCの完全同期や全フレーム誤差ゼロを意味しない |
| SaveState / LoadState | 定義した保存領域・動的状態・乱数等のスナップショット保存と復元。全メモリの無条件コピーではなく、保存完全性には未検証条件が残る |
| harness | ゲーム本体を使わず同期ロジックを実行する試験プログラム。Windowsの音声時計・実入力・実ゲームメモリは別に確認する |
| headless | 対話UIを使わず引数で動作するCLIモード。試験の自動化に利用する |
