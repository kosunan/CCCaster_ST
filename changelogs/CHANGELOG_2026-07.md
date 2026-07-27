# docs: AI向け指示ファイルを落とし穴ベースに再構成 — AGENTS.md 圧縮とガイド統廃合

## 2026-07-27: 指示ファイルの再構成

### 概要
`AGENTS.md` (9KB) と `AI_WORKSPACE_GUIDE.md` (19KB) の二重管理を解消し、
AI 向け指示を `AGENTS.md` 単一ファイル (4.3KB) に集約。内容も「コードを読めば
わかること」を全削除し、「コードを読んでもわからないこと」だけを残す方針に転換。

### 変更理由
- 両ファイルが揃って「最初に読め」と主張し、指示が競合していた
- `AI_WORKSPACE_GUIDE.md` のディレクトリツリーが実在しない構成
  (`src/app/`, `domain_netplay/`, `domain_scene/`, `src/cli/`) を説明しており、
  参照した AI が誤った前提で作業を始める状態だった
- ディレクトリ別の「規範/禁止」9セクションは、コードの配置を見れば導ける内容だった

### AGENTS.md に残した内容（プロジェクト固有の暗黙知）
- 凍結時点の状態 — 入力パイプラインは意図的に撤去済みで、未実装であってバグではない旨
- `SceneRunner::Step()` でのブロック禁止 — キープアライブが不正化し `Peer Disconnected` に至る
- `CC_INTRO_STATE_ADDR` の値の向き (2=紹介中 / 1=pre-game / 0=in-game)
- `CC_SKIP_FRAMES_ADDR` 使用禁止
- フォルダ名と namespace の対応表（過去のリネームで乖離）
- mingw32 (32bit) 必須 — mingw64 では DLL 生成に成功したうえで注入だけ失敗する
- `docs/` は現行仕様ではなく経緯の記録である旨、`tools/` (DummyPeer) の消失
- `.agents/` およびルート直下の `*.txt` / `*.log` / `*.exe` が gitignore 対象である旨

### 変更ファイル
- [MODIFY] `AGENTS.md` — 全面書き換え（9KB → 4.3KB）
- [NEW] `CLAUDE.md` — `@AGENTS.md` の1行のみ（内容の重複を作らない）
- [MOVE] `AI_WORKSPACE_GUIDE.md` → `docs/archive/AI_WORKSPACE_GUIDE_2026-03.md`
- [MODIFY] `README.md` — 実在しない `tools/` の記述を削除、`src/cli_launcher` に修正、
  `docs/` が現行仕様と乖離している旨を追記
- [MODIFY] `.agents/workflows/build.md` — デプロイ先を実在する `MBAACC_1` / `MBAACC_2`
  の2窓構成に修正（gitignore 対象のため本コミットには含まれない）
