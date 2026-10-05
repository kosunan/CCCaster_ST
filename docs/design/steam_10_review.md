# Steam移植・第10担当レビュー

2026-09-11。独立レビュー担当としてコードを読み取り確認。人数上限により第7担当を再利用。実人数は親の報告に従う。レビュー中にユーザー方針がSteam専用プロジェクトへ変更されたため、本書は `I:/work_space/CCCaster_Steam` に保存した。

対象はGameRuntime/GameBuildGuard/GameLoadedCode、入力・再戦・SFX・D3D/時計IATフック、RealGameMemoryのSteam分岐。ゲームの起動・停止・プロセスメモリ操作・ビルドはしていない。親へ重大事項を先に通知済み。

## 指摘

### P1: 多重起動回避がSteam関連DLLにも及ぶ

`src/core_dll/mbaa_mem/dllmain.cpp` の `ApplyMultiInstanceBypass()` は共通経路から呼ばれ、FindWindowA/W関数本体を常にNULL、CreateMutexA関数本体を偽HANDLE `0x1337` を返す実装へ置換する。対象ゲームEXE以外の全DLLからの呼出しにも及ぶ。Steam API、overlayなどが正規mutexを作成した場合も偽HANDLEとなり、待機・解放・終了処理が成立しない。

現状はSupportsRuntimeがSteamを拒否するので到達しない。Steamを有効化する前に旧版用の共通API改変を外し、必要ならSteamゲーム自身の多重起動チェックだけに限定する。単にSteam環境の起動成功を見てもmutex利用経路すべての検証にはならない。

### P1: フォルダ分離だけでは旧版への誤接続を拒否できない

確認時点でEdition識別はローカルEXEの照合に使われ、通信互換性識別へ渡る経路はない。旧版と同じwire版10のままSteam起動を有効にすると、別プロジェクト・別フォルダでも誤接続可能。ユーザーのクロスプレイ対象外方針に従い、Steam固有互換性ID等でゲーム開始前に拒否する必要がある。親が対応担当。

### P2: 非アクティブ時の継続動作が未移植

`MbaaPatcher::ApplyStartupPatches()` のSteam分岐は入力ソースの2箇所を変更後にreturnする。旧版の非アクティブ停止回避（旧0x40E0C0等）に対応するSteam処理はまだない。旧固定アドレスを使わないことは正しいが、バックグラウンドでも60Hzで進行する要件を満たすかは別途解析・実機確認が必要。Steamの同等チェックが存在すること自体は今回未同定なので、実機で停止したという断定はしない。

## 確認した保護と整合性

- SupportsRuntimeはSteamを拒否し、SnapshotDumperのSteam分岐もfalseで止まる。不完全な状態表でロールバックを開始しない。
- 実アドレス解決は実module base＋RVA。Addressの未対応値は0で、旧版アドレスへのフォールバックはない。現行使用マクロを全体照合し、未対応AUTO_REPLAY_SAVE/AUTO_ACTIVATEは旧版分岐内、FORCE_GOTOはコメントだけだった。
- GameLoadedCodeは既知ファイルの.textからHIGHLOW再配置後の期待値を作り、EB FE入口ロックだけを例外として比較する。相対callへ一律差分を足す処理はない。
- GameRuntimeの初期化はDllMain入口の版検証後。調べたCCマクロ利用に、版確定前の静的初期化で実アドレスを固定するコードは見つからなかった。
- 入力2箇所の置換はソースのdirection/buttonsを残し、物理入力callbackとpush引数をまとめて除去する。派生入力フィールドへの横書きは追加されていない。
- Steam再戦フックはESI/menu+38、旧版+40と区別。0/1のみをcallback前に適用し、自動保存state=5の直前では別フックでobject+CCを0へする。両署名は第7担当でディスク一致を確認済み。
- Steam SFXは元ループのESI=indexを使用し、skipは件数加算後の0x521970へ進む。元のECX=this取得はtrampoline側に残る。
- D3Dと時計のIAT走査はGameRuntimeの実base/imageSizeを使い、旧400000固定条件を除去している。

## 第7担当の自己レビュー

SteamReplayCursorはround138、inputs+11C、container1C、state8。別途ディスクのround constructor `0x499190` を再確認し、+11C/+120/+124と+12C/+130/+134がそれぞれvectorの開始/末尾/容量として0初期化されることを確認した。`0x49B950` は入力コンテナーを4個に揃える。RNG記録追加 `0x49E2A0` もDWORDのvector末尾を4B伸ばしている。

Capture/Restoreは保存時アドレスでなく再確保後の現在の入力/RNGバッファを使う。圧縮入力の最後のstateを書き戻す必要があり、8B全体が保存されている。Restoreでは全対象の境界・書込み可能性確認を先に行う。RealGameMemoryのSteam分岐へ接続済み。

ただしこれは構造とコードの静的照合。round vectorの末尾を現在roundと扱うことが、再戦・トレーニング記録を含む全状態で妥当か、実ゲームで確認が必要。一般状態表の完成前にCursor単体の成功を同期完成と扱ってはいけない。

## 検証の限界

本レビューでSteam実戦成功、ASLR後のフック稼働、非アクティブ時動作、再戦、リプレイ非生成は確認していない。署名が合うことと、ゲームスレッド上で適切なタイミングに設置・実行されることは別である。Steam専用ビルドの起動有効化は上記P1と未完成Snapshotを解決してから行う。
