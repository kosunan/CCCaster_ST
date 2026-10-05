# Steam 子ポインター資源の寿命監視

2026-09-11。担当4。既知 Steam EXE の逆アセンブルと32bit独立harnessで確認。ゲーム起動・本体ビルド・2窓試験は親担当が実施する。

## 採用方式

永久寿命の静的仮定を採用しない。`SteamSnapshotPointers.hpp` に署名付き3入口hookと単調な資源世代を追加した。`LifetimeVerified=false` は静的仮定がない意味で維持し、`AppendVerifiedNodes` は監視が正常に稼働している場合だけ3000子nodeを追加する。

保存blobにはローカルの `uint32_t` 世代を付加する。保存前に `CaptureGeneration()` を取得し、全保存後 `ValidateGeneration(saved)` を確認する。復元時は **rootを含め一切書き込む前** に世代を確認する。資源破棄・再解析が起きた古い保存はfalseで拒否する。破棄後に新しく保存した同一世代は利用できる。世代値は別窓と一致する保証がないため、MEMハッシュや同期通信に含めない。

`InstallLifetimeGuard(GameRuntime::Edition(), GameRuntime::Image())` は全体版照合後、ゲーム資源loaderを開始する前に実行する。Save/Loadと資源破棄を同時実行しない既存ゲームスレッド所有条件が必須。これ自体は別スレッドの資源破棄との排他ロックではない。

## 逆アセンブル根拠

解析元 `build_logs/steam_analysis/steam.asm`。住所は比較用preferred VA、実際のhook先は実module base＋RVA。

|入口|RVA|監視対象と根拠|
|---|---|---|
|4304E0|304E0|補助資源parser。43055CでASV0、430563..4305B4で確保・先頭pointerとVの内容設定。呼出元4316F7/431E77/4321DA。既存allocationの内容再解析も世代変更になる。|
|433570|33570|frame資源破棄。4336C6でF+38、4336CDでA先頭を取得し、4336D4でASV0 free、4336FAでA free。直接callは432265/433996/4339AA。|
|433950|33950|frame集合破棄。433996/4339AAは個別frame経路。連続pool所有モードでは個別frameがASV0をfreeしないため、433A1B..433A60の補助配列走査、433A46のASV0 freeをこの上位入口で必ず先取りする。呼出元433AB8。|

433AB0は最初に433950を呼んだ後でframe表を解放する。433C50の全体破棄、4326B4/43270Aの個別資源置換もこの経路へ到達する。これにより同一住所へのfree/reallocate（ABA）も世代不一致として拒否し、VirtualQueryだけの生存判定には依存しない。

SceneRunner.cpp の boundary は phase変更、初回、InGame intro=2への遷移を検出する。旧入力世代drain後、history.Reset、InGameならsnapshots.Resetを実行する。従って通常のモード変更・ラウンド切替は既存の履歴境界に乗る。今回の監視は、その境界の外で資源更新が起きる可能性にも古いpointerを復元しないための追加条件である。

## ABIと失敗時

フックはGCC i386のnaked関数。`pushfl; lock incl generation; [wrap時exhausted=1]; popfl; jmp *trampoline` だけを生成する。ECX/EDXを含む全汎用レジスタ、stack引数、x87/XMMは変更しない。元のflagsを復元し、元関数のcalling convention、戻り方、戻り値をそのまま使う。C/C++ callbackを呼ばないため、独自fastcallを誤宣言して引数を壊さない。

0は利用不可。32bit世代が一周したら永久に利用不可にし、古い世代番号の再使用を避ける。各入口署名には再配置operandがなく、13/14/15Bを既知EXEに照合した。版違い・署名違い・MinHook失敗時は導入false。途中失敗時に作成済みの自分のhookだけを除去する。他のhookを一括停止しない。

## 確認結果と組込み条件

`build_logs/steam_analysis/lifetime04/test.cpp` を32bit GCC15で独立コンパイルし実行成功。未導入拒否、世代一致、parser/frame/collection各hookの世代更新、旧世代拒否、3000node追加、wrap後永久拒否を確認した。生成コードobjdumpで3hookの上記命令列を確認。ヘッダを単にincludeした最小translation unitでもリンク成功することを確認し、asm参照inline変数の最適化による未定義symbolを防いだ。

実EXEから3署名を直接読み出して一致。ゲーム内実施は未確認。親担当はInstallの起動順、save blobの前後検査、loadの書込み前検査を組み込んだ上で2窓R>0を検証する。
