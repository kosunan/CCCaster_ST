# vB 1.3ネットコードのSteam版への移植

2026-09-15。作業先 `I:/work_space/CCCaster_Steam`、基準 `I:/work_space/CCCaster_verB` の `a414a39`（v1.3）。共通ロジック・構成をvBへ合わせ、ゲーム依存部分はSteam 2017-01-05から再同定した。単一エージェントで実施。元vBのHEAD・ソース・設定へ書込みはしていない。作業開始前から存在したMBAACC_Trainingサブモジュールの変更を保全している。

## 1. 構成と共通ロジック

- CMake入口は `src/CMakeLists.txt`。実装・harness・単体テストは `src/src`、依存は `build/dependencies`、実ゲームは `test/runtime`、ログは `test/logs`、参照実験は `test/source`、履歴は `docs/changelogs`。
- vBの入力時計、バッファ、予測・訂正・再送、独立選択、再戦ACK、観戦、得点と配信表示、Training保存/復元、リプレイ保存/再生、GUIを引き継いだ。
- 製品名・実行ファイル・設定・IPC・接続salt・通信識別をSteam専用へ分離。交渉STN1、同期/終了CCS1、形式版番号10。Carnival版はファイル識別・拒否試験のために残すが実行バックエンドには使わない。
- 旧server、.claude、System、build_probe、古いルートCMake等は現行ツリーから除去。未コミット変更を失わないよう `archive/steam_before_vb_20260915` に保全した。旧プロセスを名前だけで一括終了するrun_real_pairは退避し、所有プロセスだけを終了するbounded版を使う。
- 今回生成したTraining試験コピー7件は `archive/verification_runtime_20260915` へ保全し、通常のruntimeを整理した。結果JSON・ゲームログ・試験INIは `test/logs/training_fn_*` に残す。結果JSONのruntime欄は実行時の場所を表す。

## 2. Steam解析対象と主要アドレス

Steam MBAA.exe SHA256: `11270cf2da851054aa6c1bff309d50b042429abd936dca5df833bcd1de5e6c46`。
PE32/I386、preferred base 400000、SizeOfImage F3E000。以下は説明用preferred VA。実装では実ロード先＋RVAを使用し、保存済みポインターへASLR差分を二重加算しない。

| 項目 | Steam VA / 根拠 |
|---|---|
| プレイヤー | 5BC370、stride AFC。aux 5BF000、stride20C |
| sim / freeze | 5C4414 / 5C9C60 / active5C5130。47ABAD、47ABD4等 |
| intro追加 | 7ACB64、7B4528、5BF850、5B4DE8/5B4DE4。4CB8CB/4CB4DF/4CB493、4B2F0D/4B2F01 |
| script RNG | 5CAB98 index、5CAB9C予約、5CABA0の55語。4B6320/4B6341 |
| 演出RNG | 5CAAB4 index＋5CAAB8から224B。479BB0のECX=1、4FC1F6のmod3分岐、4D01C2 |
| RoundCall種別 | 5B4DD4。4FD114で0との大小を比較し、7B4DCC初期値0.5/0.4を選択 |
| ゲーム設定 | 5BB170のnativeポインター。damage +C、timer +10、wins +1C、autoReplay +28 |
| リプレイ | 本体7E9B9C、round vector7E9BFC、添字7E9C08、round stride138、input container1C |
| serializer | 49DFA0 thiscall、ECX=Replay*、引数filename、末尾ret4。標準版番号C0000004 |
| 台詞状態 | 466CE0 thiscall。script戻り先4C208D/4C20C5のみ同期フレームで判定 |
| 効果音 | update521950、dispatch52195F、skip521970。objects7D2D90、flags7D47B0 |
| 描画の最終待機 | 48B0E4のLeaveCriticalSection、48B0D6→50F170、D3D Present戻り50F2ECを局所照合 |
| End/Begin統合 | 50F96B〜50F985と50FA42〜50FAC8を全命令照合。vBと同じ状態機械で、同じdeviceの確認済み対だけを統合 |
| 起動 | native Replay枝48542C、Training/VS分岐4852F5、fade4851B8。DLL側起動DDS callsite50E904 |
| Training BGM | 4CB533の条件分岐。ゲーム再初期化によるBGM停止枝を回避 |

自動類似候補を採用するだけにはせず、命令の役割・幅・呼出規約を確認した。例えばP2 transitionの候補5CB650は誤りであり、4CB4DFが参照する7B4528を採用した。旧アドレスはマップの検索キー・比較資料としてのみ使う。

## 3. 保存と再計算で修正した点

最終保存は122断片＋3000子ノード。root 1,229,542B、pointer snapshot 1,241,542B、資源世代・native replay cursors・1500 sound-until値を含む全保存1,247,650B。

1. 音声フック設置後に元GetStatus命令を再署名検査して起動を拒否していた。設置前の署名照合をInstallReplayEffectsに集約した。
2. 未作成の最初/次roundをround=0として保存していたため、intro中にnative roundを確保した後の復元が拒否された。vBと同じ論理添字index+1を、空roundでも保持する。復元は入力/RNG終端だけを巻き戻し、native割当容量・実ポインターを保つ。
3. `[INTRO]` 診断が旧Steam版のままだったためvBの観戦比較器が演出行不足を検出した。現行vB診断をSteamアドレスへ移植した。
4. 全byte比較で7B4E60の4B差を発見。indexだけ保存されていた演出RNGへ残り224Bを追加した。
5. 次round intro2→1の再計算で7B4DCCの4B差を発見。初期化分岐の入力5B4DD4（RoundCall種別）を追加した。

強制比較はテスト環境変数だけで有効。自然な訂正がなく、直前までの入力が確定している場合のみ4F再計算し、前後の全byteを比較する。差分の除外や期待値の書換えは行わない。

## 4. 検証

| 条件 | 結果 / 根拠 |
|---|---|
| MinGW32 Release | EXE/GUI/DLLのMachine=014C。43 CTests合格。`test/logs/migration_ctest.log` |
| Python解析器 | 74 unittest合格。移動後のharnessパスを修正 |
| 保存表静的検証 | vB66＋従来追加6＋今回追加2=74ルートを全byte被覆。重複なし。4つの異なる配置・3000子ノードのSave/Load一致。実EXE根拠命令も確認 |
| 60〜96ms / loss5% / D2R4 / 85秒 | 対戦・観戦2,149確定F、REC/FRAME/STATE/MEM/INTRO差分・欠落0。双方ONCE→再戦、得点、標準保存成功。`test/logs/spectator_20260915_172322` |
| 15〜25ms / loss5% / D2R4 / 85秒、最終DLL | 対戦・観戦2,244確定F、同上差分・欠落0。片側キャラセレ優先→選択→再戦。全保存比較host42/client39回、計81回一致。`test/logs/spectator_20260915_173027` |
| Training | 保存なしFN2、通常保存、FN1押下中停止、3回FN2復元、双方ヒットストップ保存と3回復元、キャラセレ保存消去、戻った後の古い保存抑止。BGMパッチ命令一致。`test/logs/training_fn_20260915_172815/result.json` |
| 標準リプレイ再生 | 保存済みSteam replayが標準一覧に表示され、パッド決定で再生開始。16秒の実メモリ標本でintro→通常戦闘・時計進行・キャラ移動を確認。`test/logs/replay_native_20260915_173813/result.json` |
| 配置・起動導線 | 最新3成果物をMBAACC_1/2/3のcccasterへ配置、hash一致、INI不変。手動2窓スクリプトのCheckOnly成功。`test/logs/migration_final/deployment.json` |

高遅延試験はSceneMergeのSteam照合追加とRoundCall種別の追加保存より前、Training成功試験は最後のRoundCall保存追加より前のDLL。最終DLLで低遅延の全保存・観戦・片側キャラセレと標準リプレイ再生を検証した。共通Training操作ロジックは成功試験後に変更していない。途中の診断失敗を成功実績へ合算していない。

TrainingではSteam InputがDS4をXbox形式へ変換していた。列挙された個体GUID `11FF28DE-28DE-0002-0000-504944564944` を試験コピーに明示し、FN1=Back/FN2=Startで操作した。Steam全体の設定は変更していない。他ゲームが前面の場合の試験入力不達は製品の保存処理失敗と区別した。

対戦比較は絶対WT・メニューカウンターを除く代表状態。全保存比較は同一プロセスの確定同一入力に限る。全ゲームメモリ、異なるPCの全ポインター値、全キャラ、全技、全ネット環境の証明ではない。時計精度ゼロや全表示16.667ms以内も認定しない。

## 5. 成果物

バージョン1.3.0、Steam専用。ハッシュ・サイズ・テスト索引は `test/logs/migration_final/verification.json`。ゲーム本体・ユーザーINIを配布物には含めず、今回は外部公開・ZIP作成・Gitコミットを実施していない。
