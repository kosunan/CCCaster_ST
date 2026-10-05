# Steam版・第7担当: 再戦とリプレイ記録カーソル

2026-09-11。既知SHA256のSteam EXEと `build_logs/steam_analysis/steam.asm` を静的解析。ゲーム起動・停止・実メモリ操作・ビルドは行っていない。

## 実装

`NetplayMenu.cpp` は版別フックへ変更した。Steamは実ロード先＋RVAで以下2箇所を解決し、両方の署名を検証して設置する。片側の作成・有効化に失敗した場合は作成したフックを撤去する。

- 選択適用: `0x4359AF`。`8B 46 40 8B 4E 38 57 8B 0C 88 8B 01 FF 50 1C`。ESIはメニュー、選択は+0x38、項目vectorは+0x40。callback呼出し直前で0/1に限定する。旧版 `0x4299CB` とESIは同じだがオフセット・命令が違う。
- 自動保存抑止: `0x4DC0AC`。`83 BE CC 00 00 00 01 0F 85 9E 05 00 00`。ESIは再戦画面object。mode=RETRYでobject+0xCCを0にし、正規の保存state=5への遷移を抑止する。設定ファイルと記録バッファには触れない。

両署名はpefileでディスクから抽出して一致確認した。全レジスタ・flagsは既存と同じpushal/pushflで保全し、MinHook trampolineに戻る。ロード後のフック設置・再戦操作・自動保存抑止は未実機確認。

選択肢の根拠: `0x4DB6CD` から `0x4DB6F2` が順に `ONCE_AGAIN` (0x5891D4), `CHARACTER_SELECT` (0x587D30), `SAVE_REPLAY` (0x5891E0) を登録する。添字0=ONCE、1=キャラセレ、2=保存。双方ONCE/片側キャラセレの判断は既存SceneRunnerに任せる。

自動保存の根拠: `0x4DC912` で `[0x5BB170]` を読み、+0x28の設定が非0なら `0x4DC91D` で再戦object+0xCCに1を格納する。`0x4DC0AC` はこの値が1のときだけstate=5へ進める。手動保存もstate=5へ進む（0x4DC6B5〜0x4DC6CD）が選択0/1制限で防ぐ。旧固定 `0x553FE8` はSteamへ流用しない。

## Steamの記録構造

`SteamReplayCursor.hpp` に独立した `replay::Snapshot` / `Capture` / `Restore` を用意した。呼出しはゲームスレッドに限定する。親担当によるRealGameMemoryのSteam分岐への接続が必要。

|項目|旧版|Steam|命令根拠|
|---|---|---|---|
|round vector開始slot|77BF98|7E9BFC|499ADD〜499B4B|
|round vector末尾slot|77BF9C|7E9C00|同上|
|round stride|140|138|499B4B、4D0185|
|round inputs開始offset|120|11C|49D936、49C7C8〜49C7D3|
|inputs末尾/容量offset|124/128|120/124|49C7C8、構造vector|
|container stride|20|1C|49D92D〜49D93D、49C847|
|states/end/capacity|4/8/C|0/4/8|49C439〜49C43E、49C518〜49C531|
|total/total2/index|10/14/18|C/10/14|49C447、49C467〜49C46A、49C485〜49C490|
|state stride|8|8|49C44A、49C481|
|round RNG記録vector|旧版未比較|12C/130/134|499D07〜499D24|

Steamのコンテナー先頭に旧版のdebug iterator用4Bはない。旧構造を同じアドレスへ置き換えると壊れる。

Captureは4コンテナーのindex・件数・末尾offset・最後の8B状態、RNG記録vectorの末尾offsetを保存する。Restoreは全対象を先に検証し、後で伸びた記録領域をゼロにして末尾を戻す。再確保された入力/RNGの現在のポインターを使い、保存時ポインターへ戻さない。入力記録の同一状態圧縮は最後のstateのbyte+1を増やす（49C45B〜49C464）ので8B全体の復元が必要。

RNG記録は `0x499D18` でround+12Cをvectorとして `0x49E2A0` に渡して毎フレーム追加している。入力カーソルだけでなくこちらの末尾も戻す。

## 未確認

32bitビルド、ロード後署名、実際の再戦、リプレイファイルが増えないこと、再計算後に記録カーソルが一致することは親の統合検証対象。静的構造の照合を実機同期成功として扱わない。ゲームスレッド以外でのCapture/Restoreは未許可。
