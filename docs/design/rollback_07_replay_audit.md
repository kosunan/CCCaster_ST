# 第7担当: Steamリプレイカーソル・副作用の独立監査

2026-09-11。既知Steam EXEの逆アセンブル、保存復元コード、独立32bit合成メモリ試験を実施。共通build・ゲーム起動・既存プロセス停止は行っていない。

## 発見・修正

`SteamReplayCursor::Current` がround vectorの末尾を常に選択していた。実コードの入力追記 `0x49D91A` は `[7E9B9C+6C]=[7E9C08]`、RNG追記 `0x499D07` は `[7E9C08]` を添字に使う。Training初期化 `0x4996B4` は10個を先にresizeし、`0x4996F6` が任意の選択番号を保存する。末尾固定はこの場合に無関係なroundを保存する具体的な欠陥である。

Currentを実配置+RVA `0x3E9C08` の現在添字で選択するよう修正。vectorのbegin/end/capacity、添字範囲、slot可読性も確認する。末尾でないroundを選択した状態からの復元を検証した。

## 保存・再確保の照合

- `0x49C430` は8B入力stateを同一ならbyte+1で圧縮し、container+C/+10を加算。新規入力はvectorへ追加して+14を末尾添字にする。現在カーソル・末尾8Bを保存する方式は一致する。
- `0x49E2A0` はRNG値を末尾へ追加し、必要なら `0x49E500` で再確保する。保存時のheapポインターを復元せず、現在bufferとend offsetを使用する方式は一致する。
- Restoreは4入力とRNGの書込み条件を先に検証してから更新する。4番目の不正カーソルで前3名が変わらないことを確認。
- RealGameMemoryでカーソル復元後にPointerSnapshotの無検証Loadを行う順序は、子ポインターが不正な場合の部分書込みリスクがある。親へ即報告済み。親と第8担当がValidateLoadによる事前検証を実装するため、この担当では共通ファイルを変更していない。

独立試験 `build_logs/steam_analysis/replay_audit/replay_cursor_test.cpp` をMinGW32 g++で作成し、13項目全成功 (`result.txt`)。10個のround中3番目の選択、入力/RNG両buffer再確保、圧縮末尾復元、余剰末尾ゼロ、異なるround/範囲外添字拒否を含む。合成メモリ試験であり実対戦確認とは区別する。

## SFX・副作用

実EXE SHA256 `11270cf2da851054aa6c1bff309d50b042429abd936dca5df833bcd1de5e6c46` とupdate/play/stop/RNGの署名を独立比較し、85/84/55/75Bすべて一致 (`signatures.txt`)。

`0x52195F` はESIを音番号、ECXをsound objectとする。フックはレジスタ/flagsを保全し、通常は元trampolineへ戻る。抑止時は `0x521970` に入り、sound番号を加算してループを続け、最後のflagsクリアを実施する。DirectSound object自体を保存復元する実装ではない。RNG署名は単一関数のみで、インライン更新を全計測したとの主張はできない。

診断オプションの追加フック設置失敗時、Steam InstallReplayEffectsは既に設置したSFXフックを撤去せずfalseを返す。起動継続は失敗扱いとなるが再試行できない。通常の診断OFF成功経路の同期欠陥とは区別する。親へ報告した。

ゲームでの音声重複・再戦・F4操作はこの担当では確認していない。親の2窓結果をこの合成試験の結果へ混ぜない。
