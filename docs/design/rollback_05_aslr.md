# Steamの2窓起動とASLR監査

2026-09-11。担当05。独立Steamプロジェクトのみ変更。実機起動・全体ビルド・2窓同期判定は統合担当が実施する。

## 確定した多重起動阻止点

Steamのインポート表と `build_logs/steam_analysis/steam.asm` を照合した。以下はImageBase 0x400000のpreferred VA。実装ではRVAを使用する。

| 命令位置 | 処理 |
|---|---|
| 0x438C22 | 文字列0x583990「MELTY BLOOD Actress Again Current Code」をmutex名に指定 |
| 0x438C30 | IAT 0x56B108のCreateMutexAを呼ぶ。initialOwner=1、security=null |
| 0x438C36 | 本物の戻りハンドルをESIへ保存 |
| 0x438C38 | IAT 0x56B0D8のGetLastError |
| 0x438C3E | ERROR_ALREADY_EXISTS=0xB7と比較 |
| 0x438C43 | `75 2A`、既存でなければ0x438C6Fへ |
| 0x438C45〜0x438C5E | FindWindowA、既存窓の前面化 |
| 0x438C64 | 0を返してWinMain終了 |
| 0x438C6F〜0x438C70 | ESIを引数にReleaseMutex |
| 0x438CAA | 0x4296A0へ進み、通常のアプリケーション初期化 |

EXE中のCreateMutexA/FindWindowAの直接IAT参照はこの各1箇所だった。Steam DLL内の排他機構には手を加えない。

`SteamMultiInstancePatch.hpp` はRVA0x38C43の先頭1バイトだけを75→EBへ変更し、正常起動側へ進める。CreateMutexAの実行と本物のハンドルを維持する。第2プロセスは既存mutexを所有していないためReleaseMutexが失敗し得るが、戻り値は元コードでも参照しておらず、そのまま初期化する。偽ハンドルやOS API全域書換えは行わない。

局所検証範囲は0x438C22〜0x438C75の84バイト。引数、比較、両枝、return、通常側ReleaseMutexまで全て照合する。HIGHLOWの9箇所（範囲内offset 1/10/16/24/38/44/55/62/80）はPE再配置表と一致し、実baseとの差分を正確に加える。ワイルドカードは使用しない。適用済みEBも同じ周辺署名を要求する。VirtualProtect/Flush失敗時は元バイトと保護を復元し、復元失敗なら終了する。入口停止中・全体の版照合後に呼ぶ。

静的検証: 実インストールEXEの84バイトとヘッダー配列一致、実PEの9再配置位置一致、ジャンプ先RVA0x38C6F一致。32bit g++のC++20構文検査成功。これらは2窓実機成功を意味しない。

## 同期比較におけるASLR

`MbaaMemTrace::Sample` のSTATEはReadRngで得る58 DWORDのFNV値と双方XY座標。MEMはmode/intro/state/world/real/round/menuカウンター・RNG先頭・sequence/HP/round/win値である。プロセスアドレス、heapアドレス、HWNDは含まれない。この既存比較自体は異なるmodule baseで実行できる。ただしSTATEという名称でも全保存領域のハッシュではなく、STATE/MEM一致だけで全保存表の完全性を証明してはならない。

`PointerSnapshot` の保存バイトはローカルのロールバック用であり、ポインターを含む。`SceneRunner` は自プロセスのRollbackStatesへSave/Loadし、ネットワークへ保存バイトを送らない。子ノードは親内の再配置済みポインターを追う。`SteamReplayCursor` のround値もローカル資源の同一性確認用で、相手プロセスと比較する値ではない。

したがって生のSnapshot全体をそのまま2窓間ハッシュ比較してはならない。moduleポインターはRVA化できてもheapポインターはそれだけでは正規化できない。完全性確認には同一プロセス内の保存→再計算比較と、別プロセス間の意味上同じ状態項目の比較を併用する必要がある。

## 残存固定アドレスの実行経路

- MbaaPatcherの旧NOP群: Steam入力パッチ適用後の早期returnで到達しない。
- ReplayEffectsの旧SFX/RNGフック: Steam専用枝で処理し、旧版アドレスに到達しない。
- SceneFastBootの旧fadeとdebugキー読取り: `!GameRuntime::IsSteam()` で保護される。
- CombatStress: SteamのReplayEffects枝からInstallを呼ばず、mode=0。Begin/Flushの旧書込み・読取りは有効化されない。既存のCombatStressをSteam試験の成功条件に数えない。
- PrepareBattleAudioの0x40F3A0検査: SteamではSteam専用signatureを使用する短絡枝で旧アドレスを読まない。
- StartupProfile: MatchesMenuCodeで旧module baseと署名を要求し、通常のSteam配置では未実行。InitThreadの100ms待機判定にも同関数が残るが、旧アドレスへの書込み経路ではない。
- ScenePairMerge: 旧PE timestamp/sizeで拒否される。Steamでは旧End→Beginの中間対合成を利用しない。

今回の走査で通常Steam経路に無条件で到達する旧固定アドレスの書込みは確認しなかった。未完保存表は引き続き別担当が個別RVAを逆アセンブルで照合する。
