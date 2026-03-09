# ==========================================
# In-Game Hook Library (DLL)
# ==========================================

# We assume minhook and imgui targets are already defined and populated in the root CMakeLists.txt
# so we only declare our local module here.

add_library(cccaster_hook SHARED
    # === inject/ — ゲームに介入する ===
    inject/dllmain.cpp

    # === timing/ — 時間を支配する ===
    timing/DxHook.cpp
    timing/TimeHooks.cpp
    timing/WasapiClock.cpp
    timing/Metronome.cpp

    # === input/ — 入力を奪う ===
    input/DirectInputHook.cpp
    input/WndProcHook.cpp
    input/SceneInputFilter.cpp

    # === detect/ — ゲームの状態を知る ===
    detect/PhaseMonitor.cpp

    # === network/ — 相手と通信する ===
    network/UdpSocket.cpp
    network/NetworkSimulator.cpp
    network/NetplayManager.cpp
    network/GameTickCodec.cpp
    network/PacketRouter.cpp

    # === sync/ — 入力を同期する ===
    sync/NetplaySession.cpp
    sync/NetplayClock.cpp

    # === rollback/ — 過去を修正する ===
    rollback/RollbackEngine.cpp
    rollback/StateBuffer.cpp

    # === engine/ — 全体を駆動する ===
    engine/SceneRunner.cpp
    engine/GameFrameOrchestrator.cpp
    engine/SceneFastBoot.cpp
    engine/MatchScene.cpp

    # === inject/ — ゲーム固有パッチ ===
    inject/MbaaPatcher.cpp

    # === ui/ — 情報を表示する ===
    ui/OverlayRenderer.cpp
    ui/State_Ui_Logic.cpp
    ui/State_Ui_View.cpp
    ui/InGame_Ui_View.cpp
    ui/Rematch_Ui_View.cpp
    ui/NetplayOverlay.cpp
    ui/UIManager.cpp
    ui/CharaSelect_Ui_View.cpp
    ui/Controller_Ui_View.cpp
    ui/Controller_Ui_Logic.cpp
    ui/ControllerMapper.cpp

    # === 外部参照 ===
    ../cli_launcher/ConfigManager.cpp
)

target_include_directories(cccaster_hook PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Link everything to the hook DLL
target_link_libraries(cccaster_hook PRIVATE minhook imgui d3d9 dwmapi dinput8 dxguid ws2_32 mswsock ole32 winmm)
target_include_directories(cccaster_hook PRIVATE 
    ${minhook_SOURCE_DIR}/include
    ${imgui_SOURCE_DIR}
    ${asio_SOURCE_DIR}/asio/include
    ${imgui_SOURCE_DIR}/backends
)

if(WIN32)
    # DLLも静的リンクで構成し、ランタイム非依存にする
    target_link_options(cccaster_hook PRIVATE -static -static-libgcc -static-libstdc++)
endif()
