# Steam保存表・2窓同期の独立最終レビュー

2026-09-11。担当10。共通ビルドと実ゲームは操作せず、現行ソース・既存逆アセンブル・親担当の実機ログを読取り確認した。

## 統合の判定

現時点で重大な未接続・誤ったベース加算・復元前検証の欠落は検出していない。`SteamSnapshotBuilder.hpp` は4断片表と3000子nodeを組み立て、`RealGameMemory.cpp` のSteam経路から実際に利用する。保存表の完成フラグだけを変更した状態ではない。

独立Python照合で114 root断片、root合計1,229,066 byte、子12,000 byte、計1,241,066 byte。旧表の61 rootは各byteを過不足なく被覆し、Steam側のroot断片同士に重複がない。114個の根拠RVAすべてが `build_logs/steam_analysis/steam.asm` の命令境界に存在する。これは対応表の被覆確認であり、命令位置の存在だけで意味の同一性を証明した扱いにはしない。追加6領域と子構造は別の実装・逆アセンブルも確認した。

`BuildSteamSnapshotNodes` は実配置＋RVAを領域ごとに解決し、未知サイズ・範囲外・重複を拒否する。子nodeは保存blobに格納された親pointerから復元先を求め、pointerに再配置差分を再加算しない。`PointerSnapshot` は全復元先の書込み可否を先に検証する。許可キャッシュは各解決処理内で消去される。

資源世代hookはゲーム起動解放前に設置され、未設置では子表を有効にできない。保存前後の世代検査と復元開始前の検査が接続されている。世代不一致ではroot書込みも開始しない。これらはゲームスレッド上で資源更新と保存復元が並行しない既存条件に依存する。

## 危険な境界の逆アセンブル照合

住所は比較用preferred VA。実行時は実配置＋RVAを使う。

|境界|確認結果|
|---|---|
|子pointer|`004A9AE8` はobject+4をthisとする`[esi+31C]`、全体ではobject+320。続くframe+38が補助構造を指す。|
|ASV0生成|`00430563..004305B4` は10 byte確保、補助構造先頭へpointer保存、内容を設定する。最終nodeはこのデータ先頭DWORDであり、さらにpointerとは解釈しない。|
|個別解放|`004336C6` がframe+38、`004336CD` が補助先頭pointer、`004336D4` がそのfree。監視入口`00433570`の内部にある。|
|集合解放|`00433950`から個別解放へ進む経路と、連続poolの`00433A46`でfreeする経路を確認。上位入口監視により後者も世代変更になる。|
|現在のreplay round|`00499D07` が`[007E9C08] * 138`、`00499D1E` がvector先頭を加算。`0049D91A..0049D93D`も同じ添字と138/1C strideを使う。vector末尾への誤記録ではない。|
|多重起動|`00438C43`のJNE +2AをJMP +2Aへ変え、`00438C6F`の通常初期化へ進む。FindWindow側と既存窓への操作を通らず、OS全体・Steam DLLのAPIを改変しない。|

Replay cursor復元は現行の再確保済みbufferを利用し、end・index・末尾状態とRNG末尾を戻す。現在roundとの同一性・全対象範囲を事前検証してから書く。旧heap容量pointerを保存値で上書きしない。

## 実機ログの独立確認

初回 `build_logs/bounded_real_20260911_125826/rollback_comparison.json` を直接確認した。REC/FRAME/STATE/MEMはいずれも共通confirmed 1091件、差分0、missingなし。ホスト212回・クライアント218回rollback、最大深度3、失敗配列は双方空。SFX抑止58/65回。game_1.logの保存表構成ログは3114 node、1,241,066 byteで静的計数と一致し、WASAPI activeも記録されている。

最終 `build_logs/bounded_real_20260911_130135/rollback_comparison.json` と双方game logも直接確認した。親担当の試験条件は50秒、D2/R4、遅延15〜25ms、損失5%。REC/FRAME/STATE/MEMの共通confirmedは各1718件、差分・欠落0、失敗配列は空。rollbackは478/448回、最大深度4、SFX抑止151/140回。実配置は009F0000/00920000で異なり、双方がASLR preservedを記録する。資源世代17435/17425もプロセスごとに異なるため、世代を通信比較から分離する設計と整合する。

ホストgame logでは全保存blob 1,241,174 byte（root＋子に108 byteの世代・replay cursorを追加）について、確定済み同一入力による4F再計算のBEGIN/PASSを6組確認した。targetは131537、131740、131932、132125、132307、132509。`SceneRunner.cpp` の検査は再計算前の全blobとEndReplay後の全blobを全byte比較し、不一致を黙認しない。この6回は同一プロセス内の全blob再現性、1718件比較は異なる配置の2窓同期として区別する。

最終ビルド・25 CTest成功は親担当から受領し、`build/Testing/Temporary/LastTest.log`も参照した。今回の変更範囲の保存表接続・4F復元再計算・Steam同士の短時間2窓同期について、完了を妨げる指摘はない。再戦の全操作組合せ、全キャラクター固有効果、瞬間時計誤差ゼロまでは、この短時間試験から主張しない。
