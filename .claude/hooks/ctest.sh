#!/usr/bin/env bash
# ============================================================================
# PostToolUse フック — core_dll / tests を編集したら単体テストを回す
#
# 対象外のファイルなら即 exit 0（何もしない）。
# ビルドまたはテストが失敗したら exit 2 で stderr を Claude に返す。
#
# jq はこの環境に無いため、stdin の JSON は grep/sed で読む。
# ============================================================================

set -u

ROOT="I:/work_space/CCCaster_v10"

# ── 編集されたファイルのパスを取得 ──
f=$(grep -o '"file_path"[[:space:]]*:[[:space:]]*"[^"]*"' \
    | head -1 | sed 's/.*:[[:space:]]*"//; s/"$//')

# ── 対象外なら何もしない（パス区切りは \ でも / でも通るように緩く見る）──
case "$f" in
    *CCCaster_v10*core_dll*|*CCCaster_v10*src*tests*) ;;
    *) exit 0 ;;
esac

export PATH="/c/msys64/mingw32/bin:/c/msys64/usr/bin:$PATH"

# ── テスト実行ファイルのビルド（DLL 本体はビルドしない）──
if ! out=$(cmake --build "$ROOT/build" -j12 \
             --target test_game_input test_input_buffers test_netplay_clock 2>&1); then
    {
        echo "単体テストのビルドに失敗しました:"
        printf '%s\n' "$out" | grep -E "error|Error" | head -20
    } >&2
    exit 2
fi

# ── ctest ──
if ! out=$(ctest --test-dir "$ROOT/build" --output-on-failure 2>&1); then
    {
        echo "単体テストが失敗しました:"
        printf '%s\n' "$out"
    } >&2
    exit 2
fi

exit 0
