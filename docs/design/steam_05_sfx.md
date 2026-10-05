# Steam第5担当: 効果音抑止・事前準備・乱数追跡

2026-09-11。静的解析と独立ヘッダー実装まで。DLL統合・ビルド・実ゲーム実行は担当範囲外であり、実機確認済みではない。

対象EXEは `Steam_Analysis/MBAA.exe`、SHA256 `11270cf2da851054aa6c1bff309d50b042429abd936dca5df833bcd1de5e6c46`。以下は基準VAで、実コードはLoadedImageのbase+RVAで解決する。EXE・稼働中プロセスへ変更を加えていない。

## 効果音更新と抑止

旧 `0x4de200` に対応するSteam更新は `0x521950..0x5219a4`。

| 用途 | Steam基準VA | 根拠 |
|---|---|---|
| 更新入口 | 0x521950 | ESI/EDI保存、1500件走査、末尾フラグ消去 |
| フラグ配列 | 0x7d47b0 | 0x521956のbyte==1判定、0x521995の1500B memset |
| オブジェクト配列 | 0x7d2d90 | 0x52195fの `[esi*4+配列]` |
| フック位置 | 0x52195f | `mov ecx,[esi*4+0x7d2d90]` 7B |
| 抑止時の継続 | 0x521970 | 件数加算0x52196fを飛ばして次のESIへ |
| 再生 | 0x466c60 | 0x52196aから呼出、ECX=this |
| 前回フラグ保存 | 0x7d2750 | 非零件数時に1500Bをrep movsd |

ESIは旧版もSteamも効果音番号。旧版EDIはオブジェクトだが、SteamではEDIが再生候補件数で、オブジェクトはECX。**現行 `cccaster_sfx_hook` 自体はEDIを参照せず、pushal/pop alで保存するため再利用できる**。元トランポリンにSteamのECX取得命令を実行させればよい。抑止先は旧 `0x4de223` を流用せず、`base+0x121970` とする。抑止された候補は件数加算を飛ばすが、更新末尾のフラグ消去は実行され、旧方式と同じ意味になる。

新規 `src/core_dll/rollback/SteamReplayEffects.hpp` は以下を提供する。

- `Resolve(edition,image,sites)`: 更新全85Bと再生／リスト走査命令を検査し、更新・フック・抑止先・配列を返す。ゲーム全体の既知版検証は呼出元で先に行う。
- `Install(edition,image,detour,original,skip)`: 既存 `cccaster_sfx_hook`、`&cccaster_sfx_original`、`&cccaster_sfx_skip` を渡す。MinHook成功前に継続先を設定し、有効化失敗時は当該フックを削除する。
- `ValidateSoundObject(image)`: フック適用後も使える独立した事前準備用レイアウト署名。
- `ResolveRngTrace(edition,image)`: 乱数診断入口の署名確認とRVA解決。

MinHook適用はゲーム実行が対象へ到達する前の初期化時。再入／二重インストール防止は既存 `installed` 管理を使用する。SFXフック適用後に更新全体の署名照合を再実行すると失敗するため、SoundUpdate診断入口が必要なら `Resolve` の結果を保存してからInstallする。

更新全体のHIGHLOW位置は `{8,18,51,56,70}`。ASLR差分をこの位置のDWORDだけに加え、CALL相対変位は変えない。コードページの所属AllocationBase、コミット状態、実行／読取り保護も検査する。

## 事前準備とAPI計測

Steam `0x466c60` は `ECX=this` をEDIへ保存し、`[this+4]` の配列先頭からDirectSoundBufferを得る。`0x466ce0` は同じ配列を `[this+0x10]` 個走査する。DirectSoundのPlay/Stop/位置/音量APIを直接使用する現行 `SoundPrewarm.hpp` の処理はABI依存ではない。

統合時は `RealGameMemory::PrepareBattleAudio` の旧版固定PE検査を版別にし、Steamの `ValidateSoundObject` と `base+0x3d2d90` を使用する。開始演出・ゲームスレッド・停止済みバッファだけという現行条件を維持する。リストやオブジェクトに格納されたポインターは実アドレスなので再度ベース差分を加えない。

**`SoundApiProbe.hpp::Prepare` にも `0x76c6f8` が残っている。** Steamで音声診断を有効にする場合はここも版別objectsへ変更する必要がある。通常の効果音抑止だけを移して診断を有効化してはいけない。`CombatStress::Install` の旧固定アドレスも独立に移植／版ガードが必要。

SoundUpdateは両版とも引数なし・スタックを自身で復元してretするので、現行 `uintptr_t (__cdecl*)()` による透過呼出は維持できる。戻り値のゲーム上の意味は確認していない。

## 乱数追跡の範囲

整数乱数入口はSteam `0x479990..0x4799da`。状態index `0x5cb2b8`、配列 `0x5cb2bc+index*4`、呼出数 `0x5ca9c0`、最終値 `0x5ca9bc`。既存naked RNG wrapperは汎用レジスター／EFLAGSを保存し、call元を参照して元トランポリンへ戻すため、この入口への利用は可能。

署名全75BのHIGHLOW位置は `{2,20,38,45,58,65,70}`。ただし `0x4799e0`、`0x479a50`、`0x479aa0` 等には状態配列を直接更新する別関数があり、`0x479990` を呼ばない。**単一入口のログを全RNG消費ログとは扱わない**。乱数スナップショットはこれらも共有する全状態配列を保存する必要がある。またログ中のcallerは実ASLRアドレスで、プロセス間比較にはRVA正規化が必要。

## 確認した範囲

担当範囲のPython読取り確認で、ヘッダーの4署名（85/84/55/75B）すべてが実EXEと完全一致し、2組のHIGHLOW一覧もEXEの再配置テーブルと一致した。これは静的な命令・再配置検証であり、フック有効化、SFX重複抑止、音声復元、同期精度の実機試験ではない。全体ビルドとSteam対戦ログによる検証は統合担当へ引き継ぐ。
