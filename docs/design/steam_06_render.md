# 第6担当: Steam描画・時計・背景更新

対象はSteam 2017-01-05版、SHA256 `11270cf2da851054aa6c1bff309d50b042429abd936dca5df833bcd1de5e6c46`。記載VAは優先配置0x400000の値。2026-09-11、静的解析・コード変更まで実施。ゲーム起動・メモリ書込み・ビルドは担当範囲外のため未実施。

## 実装

- `DxHook.cpp`: 起動時Direct3DCreate9のIAT捕捉にあった実baseとOptionalHeader.ImageBaseの0x400000限定を除去。初期化済み`GameRuntime`の版・実base・imageサイズ、PE32、ヘッダー範囲を検証する。既存の起動gate、実CreateDevice捕捉、MinHook一括有効化と失敗時後始末、最初のEndSceneまでPresent同期を開始しない制御を保持した。
- `TimeHooks.cpp`: ゲームEXEのIATだけを書き換える方式を維持し、版・実base・PE32・imageサイズを検証。Import Descriptor、名前/slotの各RVA、比較文字列の長さに境界確認を追加。途中で不正範囲が見つかった場合はこの処理が設置したIATを復元する。4種APIが全部見つからなければ復元する既存条件を維持する。WASAPIやDLL側の独立時計には変更を加えていない。
- `ScenePairMerge.hpp`: 照合済みカニファン版のみ対象と明示。Steamでは元のEndScene/BeginSceneを呼ぶ。再計算の中間Presentを省略する既存処理は有効で、`CC_SKIP_FRAMES_ADDR`への書込みは追加していない。

## 静的確認

`build_logs/steam_analysis/steam.asm`とSteam EXEのPE import tableを使用。

| API | Steam IATの優先VA |
|---|---|
| Sleep | 0x56b05c |
| QueryPerformanceCounter | 0x56b0c4 |
| GetTickCount | 0x56b0f8 |
| timeGetTime | 0x56b344 |

固定IAT値は実装へ埋め込まず、import名から探索する。これら4種がSteamファイルに存在することを確認した。D3D9のimportも存在する。

旧SceneMergeの29B（0x4be349）と135B（0x4be497）の完全バイト列は、Steamのimage内にいずれも0件。レジスター変更だけか、区間内に追加処理があるかを確定していないため、SteamでEnd/Begin統合は未実装。

## 背景更新と旧パッチの問題

既存`MbaaPatcher.cpp`の「非アクティブ判定」欄をそのまま移せない。

- 旧0x40e0c0は実EXEでは`mov esi,0x7b0928`の即値末尾。0x40e0c1のcallを含む解放処理の途中にある。11B NOPの命令境界・意図に疑義がある。
- 旧0x4a1d42はWM_COMMAND（0x111）比較後の分岐。0x4a1d4aは`sub eax,1`の即値で、分岐opcodeではない。背景停止と判断できない。既存挙動を無断変更せず、この担当では変更していない。
- SteamのWndProcは0x438ee0。WM_ACTIVATE（6）やWM_ACTIVATEAPP（0x1c）専用分岐は見つからず、既定処理へ流れる。
- Steamは0x438d90から更新スレッド0x438cc0を作成する。UI側はPeekMessage/DispatchMessageを処理しSleep(16)。更新側は0x439fa0を反復し、そこから0x48a820へ進む。したがってUI側のSleep(16)をゲーム本体の締切と同一視しない。

以上から背景停止パッチの提案ヘッダーは追加していない。根拠のない命令変更を作る代わりに、統合したSteam独立2窓で非active側のWorldTimer/Present/確定フレームが進むか確認し、停止する場合に実際の分岐を絞る。背景進行・60Hz締切・ロールアップ・音声時計は実機未確認。

## 統合時の確認点

`GameRuntime`はDxHook/TimeHooks初期化より前に既知版で初期化済みであること。未知版はIATへ触れない。ASLR再配置の異なる起動でIAT捕捉ログ、4種時計IAT成功ログ、SteamではSceneMerge無効ログを確認する。現行カニファン版のIAT起動短縮と通常の描画機能も短い実機比較で確認する。
