# 第4担当: Steamの使用中データ参照

2026-09-11。`SteamAddressMap.hpp` は旧preferred VAからSteam 2017-01-05のpreferred VAへ変換する。実行アドレスは親の版識別・範囲検証を経て「実ロード先 + preferred VA - 0x400000」とする。戻り値0は未解決であり、旧アドレスに戻してはならない。ポインター変数の中身は既に再配置されている。

担当内ではゲームの起動・停止・書込み、共有ソースの変更、ビルドは行っていない。以下は**静的解析**であり、実機で各操作を確認したという意味ではない。従来の読取り観測結果は `2026-09-11_steam_port.md` を参照。

## 今回の新しい照合

参照した逆アセンブルは `build_logs/steam_analysis/{legacy,steam}.asm`。EXEは同ディレクトリーのidentity JSONにあるSHA256の既知2版。命令比較JSONは候補を探す補助として使い、意味が異なる対応を除外した。

|旧preferred VA|Steam preferred VA|照合根拠（命令VA）|
|---|---|---|
|0x74d8ec / 0x74d910|0x7b4424 / 0x7b4448|旧0x427927〜0x42796eとSteam0x48033e〜0x480381。各側の隣接状態を負値で除外し、選択階層0〜2を判定して小さい方を採用。続く左右走査も0x24刻み。|
|0x559580|0x5c07d8|旧0x426630〜0x426647とSteam0x47dcf1〜0x47dd1f。P2勝数+1と勝数上限を比較してgame pointを設定。前に同じP1処理がある。|
|0x767440|0x7cdab4|旧0x401e78〜0x401eb6とSteam0x494dd0〜0x494e43。メニュー要素の破棄・解放後にカウンターを減らす。Steamではsub 1からdecへ変化。多数の解放処理が同じ値を操作。|
|0x74d99c|0x7b450c|旧0x41dec4〜0x41dee9とSteam0x474cf7〜0x474d18。動作モード2を除外したあとスキップ可能状態を判定。後段の条件と処理呼出しも対応。|
|0x67bd78|0x684cb4|旧0x4265d4〜0x4265e1とSteam0x47dca9〜0x47dcb3で粒子配列をクリアした直後に0設定。旧0x4565dcとSteam0x4adbbdで最大0x1f4比較。Steamでは粒子カウンターが配列の末尾側へ移動している。|
|0x5552a7|0x5bc4e7|Steam0x4cc1c3〜0x4cc222。enabled(root+0)が有効な4人を0xafc刻みで走査し、+0x177の入力無効状態を更新。|
|0x5552a8|0x5bc4e8|Steam0x4a767d〜0x4a7720。root+0x178を軸にenabled・所有関係・puppet状態1を判定。0x4ba32a等にも直接参照。|
|0x564b14 / 0x564b18|0x5cb63c / 0x5cb640|旧0x44bb84〜0x44bba0とSteam0x4a1de4〜0x4a1e04。実時計を3で剰余した条件の直後にカメラ状態を別バッファへコピー。SteamはX/Yをmovqで8Bコピー。初期化0x4a1500〜0x4a153eも対応。|
|0x5595b8 / 0x5585f8|0x5c079c / 0x5bf83c|旧0x477f09/0x477f17とSteam0x4d0889/0x4d0897。表示条件の対の参照。さらに設定切替の旧0x47a2dc以降とSteam0x4d2f9e以降が対応。|

プレイヤー配列のストライド0xafcは実命令でも同一。ヘッダー中のP2〜P4フィールドは、**確認済みの同一プレイヤー構造とストライドの範囲だけ**から求めた。イメージ全体を一定差分で変換したものではない。

## 以前の確認を採用した項目

ゲームモード、次モード、world timer、pause、intro、ラウンド時計、round count、game point、P1 wins、training/versus pause、game state、dummy、RNG4領域、raw input/providerのポインター変数、プレイヤーroot/sequence/HP/X/Y、SFX flags/objectsは親の初期調査と各命令参照に対応する。raw inputの方向+0x18 / ボタン+0x24と、P2の+0x2c / +0x38はポインターの中身に対するオフセットであり、この関数で再変換しない。

## 誤候補・残件

- 自動候補のselector mode `0x74d8ec→0x5bb800` は不採用。Steam0x467821以降で0x38×16要素の入力デバイス状態として初期化され、0x468075以降で方向やボタンの変換を行う別配列。正しいselectorは上表の0x7b4424/0x7b4448。
- `0x555140→0x5bc378` というユーザー表は、現在コードのsequence参照と一致しない。配列rootは0x5bc370、sequenceの直接参照は0x4c6589等の0x5bc380。ユーザー表のフィールド名と同一視しない。
- `0x67bd78→0x6e28d8` のようなObj付近への平行移動はしない。今回のhit sparksは0x684cb4。
- auto replay save `0x553fe8` とreplay round end `0x77bf9c` は本担当では未解決のため0。旧auto replay saveは.text内の直接参照がなく設定ブロック経由の追跡が必要。専任のメニュー／リプレイ担当へ委ねる。
- 未使用の多数のCC_*定数、WindowProc/ForceGoto等のコードVA、禁止されたskip frames VAは未登録。必要な使用箇所があれば個別解析が必要。
- スナップショットの領域全体の長さ・子ポインターABIやリプレイカーソルはこのマップの保証範囲外。別担当のlayout解析と統合する。

## 最小検査

Pythonで全caseの重複がないこと、Steam VAが既知イメージ範囲内にあること、P2〜P4の個別フィールドが確認済みrootの+0xafc刻みに一致することを確認。実ビルド・実機テストは主担当が統合後に実施する。
