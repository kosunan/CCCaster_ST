# ==========================================
# In-Game Hook Library (DLL)
# ==========================================

# We assume minhook and imgui targets are already defined and populated in the root CMakeLists.txt
# so we only declare our local module here.

add_library(cccaster_hook SHARED
    # === mbaa_sync (MBAA同期処理 — DLLエントリ) ===
    mbaa_sync/dllmain.cpp

    # === platform (低レイヤー基盤) ===
    platform/hooks/DxHook.cpp
    platform/hooks/TimeHooks.cpp
    platform/hooks/DirectInputHook.cpp
    platform/clock/WasapiClock.cpp
    platform/network/UdpSocket.cpp
    platform/network/NetworkSimulator.cpp

    # === fg_netplay (格闘ゲームネット対戦エンジン) ===
    fg_netplay/sync/SyncCoordinator.cpp
    fg_netplay/sync/Metronome.cpp
    fg_netplay/sync/NetplayClock.cpp
    fg_netplay/sync/NetplayManager.cpp
    fg_netplay/overlay/OverlayRenderer.cpp
    fg_netplay/overlay/State_Ui_Logic.cpp
    fg_netplay/overlay/State_Ui_View.cpp
    fg_netplay/overlay/InGame_Ui_View.cpp
    fg_netplay/overlay/Rematch_Ui_View.cpp
    fg_netplay/overlay/NetplayOverlay.cpp

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
    mbaa_sync/overlay/UIManager.cpp

    # === mbaa_game (MBAAゲーム固有) ===
    mbaa_game/monitor/GameMonitor.cpp
    mbaa_game/state/StateBuffer.cpp
    mbaa_game/patcher/MbaaPatcher.cpp
    mbaa_game/ui/CharaSelect_Ui_View.cpp
    mbaa_game/ui/Controller_Ui_View.cpp
    mbaa_game/ui/Controller_Ui_Logic.cpp
    mbaa_game/ui/ControllerMapper.cpp

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
