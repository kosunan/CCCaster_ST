# ==========================================
# Overlay 単体テスト
# ==========================================
# ゲーム本体・DLL・DirectInput に依存しない純粋ロジックテスト。
# ControllerMapper は stub で置換。

add_executable(test_overlay
    test_overlay.cpp
    stub_controller_mapper.cpp
    stub_hooklog.cpp
    ${CMAKE_SOURCE_DIR}/src/core_dll/feature_overlay_ui/overlay/OverlayRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/core_dll/feature_overlay_ui/NetplayOverlay.cpp
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
