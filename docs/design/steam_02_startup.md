# Steam版・第2担当: 起動とメニューの移植

2026-09-11。10人分担の第2担当。実装した部品と静的解析の結果。ゲームの起動、終了、プロセスメモリの書込み、ビルドは担当していない。既存設定・ゲームデータは変更していない。

## 実装

`src/core_dll/mbaa_mem/SteamMenuPatch.hpp` にTraining/Versusの正規分岐へ進む部品を追加した。共通コードへの接続、Steam起動成功・実戦成功は親の統合作業と実機検証が必要。

- ASLRを維持し、`LoadedImage` の実ロード先とRVAで解決する。
- 呼出側が版と全コードを検証した後、ゲームスレッドで `Apply(image, BootMode::Training/Versus)` を実行する。
- パッチ先はSteam基準VA `0x4852F5` の `85 C0`（test eax,eax）。Trainingは `EB 3B`、Versusは `EB 74`。既に別モードへパッチ済みの変更は拒否する。
- 各分岐先の41B全体を照合。命令中の絶対参照3箇所は実ロード先で算出し、ワイルドカードにしない。書込み保護と命令キャッシュを処理し、不完全な適用時は元命令へ復元する。
- 正規分岐が引数採取・初期化呼出し・次モード予約・reset・スタック復元を行う。DLLから初期化関数を直接呼んだりmode=20を書いたりしない。
- CPU分岐は2B短分岐の範囲外なのでこの部品では対応しない。旧版の `EB 5C` を流用しない。

## 静的解析根拠

EXE: `_TEST_MBAACC/Steam_Analysis/MBAA.exe`。SHA256 `11270cf2da851054aa6c1bff309d50b042429abd936dca5df833bcd1de5e6c46`。命令は `build_logs/steam_analysis/steam.asm`、比較は同 `legacy.asm`。表のアドレスは全てディスク上の基準VAで、実行時はRVA化する。

|用途|旧版|Steam|根拠|
|---|---|---|---|
|メインメニュー振分け|0x42B475|0x4852F5|SteamはARCADE_MODE文字列照合後のtest eax,eax|
|Training分岐|0x42B499|0x485332|0x5867CCの文字列TRAINING_MODE、0x483ED0へのcall|
|Versus分岐|0x42B4B6|0x48536B|0x5867F4の文字列VS_PLAYER、0x483F60へのcall|
|CPU分岐|0x42B4D3|0x4853A4|0x586800の文字列VS_CPU、0x483FC0へのcall|
|Training初期化|0x42A160|0x483ED0|引数1個、ret 4、mode kind=0x1010、対戦フラグ0|
|Versus初期化|0x42A1B0|0x483F60|引数1個、ret 4、mode kind=1、対戦フラグ1|
|CPU初期化|0x42A200|0x483FC0|引数1個、ret 4、mode kind=0x11|
|共通初期化呼出先|0x418030 / 0x448350|0x46EB20 / 0x49F280|Training/Versus/CPU全てで呼出す|
|mode kind|0x562A74|0x5CA794|各初期化のmov dword|
|対戦フラグ|0x77BF2C|0x7E9B94|初期化前0、Versus/CPUでは共通準備後1|
|選択プレイヤー|0x55DF0F|0x5C9C97|分岐でbyteをzero-extendしてpush、初期化がalを格納|
|プレイヤー準備フラグ|0x74D998 / 0x74D9B4|0x7B4508 / 0x7B4524|Training/Versus/CPU末尾で両方1|
|次モード|0x55D1D0|0x5CA9B4|正規分岐末尾で20を予約|
|reset|0x55DEC3|0x5C9C96|正規分岐末尾でbyte 1|
|メニューオブジェクト|0x76E6D4|0x5CB5B0|0x4858CFでポインター+0x78==4、0x4858DAで振分け呼出し|

`0x49F140/0x49F180`にも類似初期化があるが、両プレイヤー準備等の副作用が異なる。単にmode kindが一致する関数を置換入口にしない。

## 検証範囲

Python/pefileでディスクEXEから分岐の各41Bを抽出し、実装と同じ署名・絶対オペランドを比較して両方一致。Training/Versusの2B分岐先算術を実base `0x400000`, `0x320000`, `0x10000000`で確認。文字列ARCADE_MODE/TRAINING_MODE/VS_PLAYER/VS_CPUをEXEから読んで枝の意味を確認した。

これは静的な署名と分岐計算の確認。ロード後のメモリ書込み、保護失敗復元、実際のキャラセレ到達は未検証。ヘッダーの32bitビルドも親の統合時に実施する。

## 未移植の境界

旧StartupPatchのSetBootFadeをSteamへ使ってはいけない。Steamはメニューstateがオブジェクト+0x78、フェードが+0x80で、SSEを用いる。通常のstate 3更新は0x5296FFで+0x8Cからxmm1を読み、push 4後0x529590を呼ぶ。旧の+0xC0やdouble定数オペランドへのパッチは対応しない。この担当はフェード変更を実装していない。

旧NetplayMenuの0x4299CB/ESI/+0x40もそのまま使用不可。Steamのメニュー群は選択値+0x38とvector+0x40/+0x44を使用する箇所がある（0x529649〜0x529671）。ただし再戦中の対象オブジェクトとcallback直前の正確なフック位置、SAVE REPLAYとONCE/CHARA SELECTのindex対応は未同定。一般メニューのフィールド候補を再戦フックへ昇格させていない。0x4830D0も+0x38を参照するが、mode kind=0x10に限定された別経路なので流用しない。

双方ONCEでのみ再戦、片側キャラセレ優先、未選択時B遮断の決定は既存Scene/同期処理を維持する。Steamの自動保存の設定先・手動保存抑止のフックを確認するまではネット対戦機能の完成としない。
