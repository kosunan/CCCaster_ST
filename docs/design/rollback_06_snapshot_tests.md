# Steam保存表の独立検査

2026-09-11。担当6。新規 `src/tests/test_steam_snapshot.cpp` を32bit MinGWで単独コンパイルし、実Steam EXEを読取り専用で指定した。ゲーム起動、共通buildへの出力、元プロジェクトの編集は行っていない。

結果は **48,239 checks / 0 failures**。ログは `build_logs/steam_analysis/rollback_06_snapshot_tests.log`。

## 検査対象

- 旧 `GameSnapshotLayout` から実際の61 rootを取得し、旧RealGameMemoryの追加6領域を独立した期待値として追加。新しい4断片表の114領域が、旧67 rootの **1,229,066 bytes** を各byte一度だけ対応づけることを確認。旧root外へのはみ出し、旧byteの欠落・重複、Steam RVA同士の重複、image外参照がない。
- 本物 `BuildSteamSnapshotNodes` が114 rootと3,000子node、合計 **3,114 nodes / 1,241,066 bytes** を生成。各子の親、source、offset、sizeを検査した。
- 32bitプロセス内で4領域を同時に確保し、異なるbaseで本物 `PointerSnapshot` の保存・復元を行った。`Configure(nodes, true)` で本番と同じメモリー範囲検査を有効にした。各rootの全byteを変更し、保存対象になった子pointer slotも無効値へ変更した後、保存blobの親値で復元先を事前検査して再保存した全blobが一致した。
- 1,000 objectを4群に分け、object pointerがnull、frameの補助pointerがnull、補助構造のASV0 pointerがnull、3段すべて有効という各枝を250個ずつ検査。最終段には0xFFFFFFFFを置き、これをさらにpointerとして参照しないことも確認した。
- 監視未設置、異なる資源世代、世代0、世代枯渇を拒否。image size不一致、RVA解決時の整数overflow、不正blob長も拒否した。
- 実Steam EXEの版識別を通した上で、114断片すべての根拠命令付近のoperandを確認。7断片は直接VAではなく、初期レジスター値・変位・loop strideで住所を作るため、その命令位置と算術を別途照合した。例: RNGは1始まりindexなのでoperand 0x5CB2BCに4を足した0x5CB2C0が保存先。

最終試験時baseは0x01620000、0x02580000、0x034E0000、0x04440000。これはテストプロセス内の合成imageであり、4回のゲーム起動を意味しない。

## 実行方法

```powershell
& C:/msys64/mingw32/bin/g++.exe -std=c++20 -O2 -Isrc -Ibuild_dependencies/minhook-src/include src/tests/test_steam_snapshot.cpp -o build_logs/steam_analysis/test_steam_snapshot.exe -static-libgcc -static-libstdc++
& ./build_logs/steam_analysis/test_steam_snapshot.exe 'I:/SteamLibrary/steamapps/common/MELTY BLOOD Actress Again Current Code/MBAA.exe'
```

CMakeは親担当へ登録を依頼した。実EXEを渡さない場合も保存表・32bit復元の試験は実行するが、命令operand照合は未実施と表示する。

## 確認範囲

この試験ではMinHookの設置済み状態だけをテストプロセス内で模擬している。本物BuilderとPointerSnapshotのデータ経路を使うが、資源hookの実行・ゲームのheap所有権・保存表外のSteam追加状態は証明しない。断片のbyte coverageと根拠operand一致は、その状態集合で実ゲーム再計算が決定的になることの証明ではない。2窓の実戦同期、資源破棄境界、実際のロールバック回復は親担当の実機検証で区別して記録する。
