# ==========================================
# In-Game Hook Library (DLL)
# ==========================================

# We assume minhook and imgui targets are already defined and populated in the root CMakeLists.txt
# so we only declare our local module here.

add_library(cccaster_hook SHARED
    # === mbaa_sync (MBAA同期処理 — DLLエントリ) ===
    mbaa_sync/dllmain.cpp

    # === mbaa_sync/common (OSフック基盤) ===
    mbaa_sync/common/DxHook.cpp
    mbaa_sync/common/TimeHooks.cpp
    mbaa_sync/common/DirectInputHook.cpp

    # === fg_netplay/network (通信基盤) ===
    fg_netplay/network/UdpSocket.cpp
    fg_netplay/network/NetworkSimulator.cpp
    fg_netplay/network/NetplayManager.cpp

    # === fg_netplay/common (クロック基盤) ===
    fg_netplay/common/WasapiClock.cpp

    # === fg_netplay (格闘ゲームネット対戦エンジン) ===
    fg_netplay/sync/SyncCoordinator.cpp
    fg_netplay/sync/Metronome.cpp
    fg_netplay/sync/NetplayClock.cpp

    # === ui (オーバーレイUI統合) ===
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

    # === mbaa_sync (MBAA同期処理) ===
    mbaa_sync/protocol/SyncCalculator.cpp
    mbaa_sync/protocol/PacketRouter.cpp
    mbaa_sync/orchestrator/SceneRunner.cpp
    mbaa_sync/orchestrator/GameFrameOrchestrator.cpp
    mbaa_sync/scene/SceneFastBoot.cpp
    mbaa_sync/scene/SceneInputFilter.cpp
    mbaa_sync/scene/SceneBusiness.cpp
    mbaa_sync/rollback/RollbackEngine.cpp
    mbaa_sync/input/InputHook.cpp

    # === mbaa_game (MBAAゲーム固有) ===
    mbaa_game/monitor/GameMonitor.cpp
    mbaa_game/memory/StateBuffer.cpp
    mbaa_game/memory/MbaaPatcher.cpp

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
