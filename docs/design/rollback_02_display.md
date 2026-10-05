# RoundCall 表示状態の保存断片照合

2026-09-11。担当範囲 node3050〜3056 / 3058 の旧104Bを、Steamの104Bへ23断片で対応付けた。逆アセンブルによる静的照合であり、実機保存復元・同期確認は統合担当で実施する。元ゲーム、旧プロジェクト、起動中プロセスは変更していない。

## 関数の同定

旧EXEの0x53AF18にある `.\\grp\\RoundCall_AA\\round\\` はSteam0x58A980、`%srd_font00_01` は旧0x53AF34 / Steam0x58A99C、`%srd_noise00` は旧0x53AF44 / Steam0x58A9AC。同一リソースと、0xA0 allocation・要素数10の描画コンテナ操作から旧0x4AC270〜0x4ACA21とSteam0x4FB4F0〜0x4FBDD2を対応付けた。コンテナ旧0x74E808はSteam0x7B4E10。ハンドルやヒープの新しい保存範囲は追加しない。

## 対応と命令根拠

VAはpreferred base 0x400000で表記。コードの表はSteam RVAを保持する。各要素4B、コンテナの4 DWORDクリアだけ16Bにまとめている。

|旧VA|Steam VA|意味を確かめた命令|
|---|---|---|
|74E7F8|7B4DFC|旧4AF0CB〜4AF110 / Steam4FE986〜4FE9BA。補間関数後の160倍値を別変数と共有してから閾値で160.0に固定。旧74E7D0→Steam7B4DD8と異なる後半の書込み先も照合。|
|74E7FC|7B4DA4|旧4AF354〜4AF37C / Steam4FEB59〜4FEB84。比率を作り1から引いて保存。同時に別変数74E7A4→7B4DACへ比率×0.5+定数を保存。終了枝で0。|
|74E800|7B4DA0|旧4AF3F5〜4AF478 / Steam4FEBF7〜4FEC44。時間から74E7CC→7B4DCCを減算、0.5より小さければ2倍の上限1、以後1に固定。74E5B0→7B4BE0の更新も一致。|
|74E804|7B4E2C|旧4AC945/4ACA1B / Steam4FBD25/4FBD6C。前半の74E830との共有値、後半の拡大率計算を照合。|
|74E80C|7B4E14|旧4AC2B5 / Steam4FB51Fで要素数10。旧4AC799 / Steam4FBABDでコンテナ再構築に使用。|
|74E814〜74E820|7B4E1C〜7B4E28|旧4AC7D0〜4AC7E2 / Steam4FBAFF〜4FBB1Dが描画コンテナの同じ4 DWORDをクリア。|
|74E824|7B4E30|旧4AC87C〜4AC89F / Steam4FBC58〜4FBC75。初期1から減衰、0でclamp。|
|74E82C|7B4E34|旧4AC8BC〜4AC8FF / Steam4FBCAE〜4FBCD1。初期0.5、時間差を割り上限1、その0.5倍+0.5。描画alpha変換も旧4AC3F3 / Steam4FB6DA。|
|74E830|7B4E40|旧4ACA07〜4ACA09 / Steam4FBD4E〜4FBD55。時間差比率の上限1を1から引く。初期区間の共有代入も一致。|
|74E838|7B4E48|旧4AC9D2 / Steam4FBDC7の整数カウンター増加。|
|74E83C|7B4E44|旧4AC8A5/4AC9DA / Steam4FBC7F/4FBDBCの経過float。|
|74E840|7B4E4C|旧4AC929/4AC95C/4AC9E4 / Steam4FBCEC/4FBD11/4FBD35の区間別1/0/1。|
|74E844|7B4E54|旧4AC8AD / Steam4FBC87の時間差の減数、初期1。|
|74E848|7B4E50|旧4AC84B〜4AC876 / Steam4FBC2A〜4FBC4E。別の速度で1から減衰し0でclamp。|
|74E850|7B4E58|旧4ACEAD〜4ACEFE / Steam4FC191〜4FC1C1。12未満の間、時間を定数で割り上限値との比較後に整数化。|
|74E854|7B4E60|旧4ACF1C〜4ACF67 / Steam4FC1DD〜4FC213。状態3で経過時間と比較、乱数mod3により次の待ち時間を選ぶ。|
|74E85C|7B4E64|旧4ACF8B〜4ACFB8 / Steam4FC231〜4FC25E。時間/定数を整数化し符号付きmod2を0/1へ変換。|
|74E860|7B4E68|旧4ACFC5 / Steam4FC26F。上記12区間の時間とmod2時間を共有する更新値。|
|74E864|7B4E70|旧4ACF14/4ACFBD / Steam4FC1D0/4FC263。状態3判定のfloat時間、状態遷移時0へ戻す。|
|74E868|7B4E6C|旧4ACF0D/4ACF7E / Steam4FC1CB/4FC22C。状態3→0と他区間の整数増加。|
|76E780|7B4E08|旧4AC995〜4AC9A4 / Steam4FBD7D〜4FBD8D。SFX番号0x59を一度だけ鳴らすフラグ。SFX配列旧76E008 / Steam7D47B0からの同じ差を確認。|
|76E784|7B4E7C|旧4ACE90/4ACFCD / Steam4FC178/4FC27B。表示シーケンス全体の経過時間。初期化旧4B17F7 / Steam5011F5も照合。|
|76E788|7B4E78|旧4ACDEB〜4ACDF4 / Steam4FC0E0〜4FC0F7の0→1。外部旧45C10E〜45C17A / Steam4B2E6F〜4B2EA0の1→2も一致。|

実装: `src/core_dll/rollback/SteamDisplayFragments.hpp`。旧連続区間をSteamで連続と仮定せず、3つの表示処理で分散・順序変更された変数を個別に保存する。全体のCompleteや起動許可は変更しない。
