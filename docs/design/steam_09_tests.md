# 第9担当作業: Steam再配置・局所署名テスト

2026-09-11。第9サブエージェントの作成は同時数とは別の総人数制限で拒否されたため、第6担当が引き続き担当した。実人数は増えていない。

## 結果

`src/tests/test_steam_port.cpp`を追加し、32bit MinGWで独立コンパイル。実EXE2本を読み、**109 checks、0 failures**。最終ログは`build_logs/steam_analysis/tests09/result.txt`、実行ファイルは同ディレクトリの`test_steam_port.exe`。ゲームを起動せず、ゲームプロセスへの書込みや関数呼出しをしていない。

当初はカニファン版も再配置可能と期待して7件失敗したが、同版にはrelocがないためテストの期待値が誤っていた。優先base以外を拒否する期待値へ修正し、再実行した上記結果が最終結果。製品コード`GameLoadedCode.hpp`はこの担当では変更していない。

作業完了後、ユーザー指定によりプロジェクトが`I:/work_space/CCCaster_Steam`へ分離された。ソース・最終ログ・検証バイナリは新プロジェクトへコピー済み。最終ログの相対EXEパスは、分離前の検証用コピーを示す。カニファン版を読み込む検査は版識別の回帰検査であり、クロスプレイを実装・許可するものではない。

## 検査範囲

- PEの全セクションを独立バッファへ配置し、HIGHLOWの再配置を適用する参照ローダーをテスト側に実装。被検関数のFileOffset/.text生成は使わない。
- Steamのbase `0x400000`、`0x320000`、`0x10000000`、`0x70000000`で`ValidateLoadedCode`が成功。
- 指定baseとコードの不一致、base=0、4GBを超えるimage、短い.text、コード改変を拒否。
- 入口の`EB FE`だけを許容し、許可フラグOFFと`EB FD`は拒否。
- reloc blockの短すぎる長さ、巨大長さ、未対応型、image外アドレス、末尾欠落、無効RVAを拒否。
- Steam Inputの2箇所について、本体署名と再配置された入力providerオペランドを確認。
- `SteamMenuPatch::MatchesStartup`を再配置済みメモリで実行。正常署名成功、分岐改変拒否。
- 独立バッファの保護属性を実行読取りにして、`SteamReplayEffects::Resolve`とRNG入口署名を実行。dispatch/skipの解決値、異なる版の拒否、改変署名の拒否を確認。ゲームコード自体は実行しない。

## CMake登録

`steam_port`テストを追加。実EXEを配布物へ含めず、任意のローカルファイルをキャッシュ変数で指定する。

```powershell
cmake -S . -B build -DCCCASTER_TEST_LEGACY_EXE="検証用旧版EXEの絶対パス" -DCCCASTER_TEST_STEAM_EXE="検証用Steam EXEの絶対パス"
```

引数を指定しない場合は空入力の拒否2件だけを実行し、実EXEによる再配置・署名検証が未実施であることを出力する。109件成功は実EXE2本を明示して得た結果であり、引数なしCTestの成功と混同しない。

共有buildとの競合を避け、担当内ではCMake再構成を実行していない。製品の統合ビルド、実入力、非active背景更新、時計、ロールバック、ネット対戦の決定性はこのテストの対象外。
