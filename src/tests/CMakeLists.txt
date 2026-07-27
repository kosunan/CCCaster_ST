# ==========================================
# 単体テスト
# ==========================================
# ゲーム本体・DLL・DirectInput に依存しない純粋ロジックテスト。
# 対象が要求する外部シンボルは stub_*.cpp で置換する。
#
# 実行: ctest --test-dir build --output-on-failure

add_executable(test_overlay
    test_overlay.cpp
    stub_controller_mapper.cpp
    stub_hooklog.cpp
    ${CMAKE_SOURCE_DIR}/src/core_dll/ui/OverlayRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/core_dll/ui/NetplayOverlay.cpp
)

target_include_directories(test_overlay PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ImGui は OverlayRenderer が include するため必要（ヘッダーのみ使用）
target_link_libraries(test_overlay PRIVATE imgui)
target_include_directories(test_overlay PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# テスト用マクロ定義（friend アクセス有効化）
target_compile_definitions(test_overlay PRIVATE NETPLAY_OVERLAY_TEST)

if(WIN32)
    target_link_options(test_overlay PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ── 入力の符号化（依存ゼロ・ヘッダオンリー）──────────
add_executable(test_game_input
    test_game_input.cpp
)
target_include_directories(test_game_input PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ── 画面ごとの入力制約 ────────────────────────────
add_executable(test_scene_input_filter
    test_scene_input_filter.cpp
    ${CMAKE_SOURCE_DIR}/src/core_dll/engine/SceneInputFilter.cpp
)
target_include_directories(test_scene_input_filter PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ── 入力バッファ（依存ゼロ・ヘッダオンリー）──────────
add_executable(test_input_buffers
    test_input_buffers.cpp
)
target_include_directories(test_input_buffers PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ── NetplayClock（θ推定・α補正）────────────────────
add_executable(test_netplay_clock
    test_netplay_clock.cpp
    stub_wasapi_clock.cpp
    ${CMAKE_SOURCE_DIR}/src/core_dll/sync/NetplayClock.cpp
)
target_include_directories(test_netplay_clock PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ── PhaseMonitor（seam 経由でゲーム無しに検証）────────
# RealGameMemory はリンクしない。実アドレスを触るコードが混ざっていれば
# リンクエラーかクラッシュで露見する。
add_executable(test_phase_monitor
    test_phase_monitor.cpp
    ${CMAKE_SOURCE_DIR}/src/core_dll/mbaa_mem/PhaseMonitor.cpp
    ${CMAKE_SOURCE_DIR}/src/core_dll/mbaa_mem/GameMemory.cpp
)
target_include_directories(test_phase_monitor PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    foreach(t test_game_input test_input_buffers test_netplay_clock test_phase_monitor
             test_scene_input_filter)
        target_link_options(${t} PRIVATE -static -static-libgcc -static-libstdc++)
    endforeach()
endif()

# ── ctest 登録 ──────────────────────────────────────
add_test(NAME game_input    COMMAND test_game_input)
add_test(NAME input_buffers COMMAND test_input_buffers)
add_test(NAME netplay_clock COMMAND test_netplay_clock)
add_test(NAME phase_monitor COMMAND test_phase_monitor)
add_test(NAME scene_input_filter COMMAND test_scene_input_filter)
