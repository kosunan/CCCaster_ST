# ==========================================
# In-Game Hook Library (DLL)
# ==========================================

# We assume minhook and imgui targets are already defined and populated in the root CMakeLists.txt
# so we only declare our local module here.

add_library(cccaster_hook SHARED
    dllmain.cpp
    # === adapter_os_hooks (OS/DirectX非依存インフラ) ===
    adapter_os_hooks/api_hook/DxHook.cpp
    adapter_os_hooks/api_hook/TimeHooks.cpp
    adapter_os_hooks/input/InputHook.cpp
    adapter_os_hooks/input/DirectInputHook.cpp
    adapter_netplay/timer/WasapiClock.cpp
    adapter_netplay/timer/NetplayClock.cpp
    adapter_netplay/SyncCoordinator.cpp
    # === adapter_network (通信インフラ) ===
    adapter_network/UdpSocket.cpp
    # === feature_overlay_ui (旧 infra/overlay + domain_UI) ===
    feature_overlay_ui/overlay/OverlayRenderer.cpp
    feature_overlay_ui/UIManager.cpp
    feature_overlay_ui/State_Ui_Logic.cpp
    feature_overlay_ui/State_Ui_View.cpp
    feature_overlay_ui/CharaSelect_Ui_View.cpp
    feature_overlay_ui/InGame_Ui_View.cpp
    feature_overlay_ui/Rematch_Ui_View.cpp
    feature_overlay_ui/Controller_Ui_View.cpp
    feature_overlay_ui/Controller_Ui_Logic.cpp
    feature_overlay_ui/ControllerMapper.cpp
    feature_overlay_ui/NetplayOverlay.cpp
    # === pure_sync_engine (同期/ロールバック計算) ===
    pure_sync_engine/RollbackEngine.cpp
    pure_sync_engine/InputFilter.cpp
    # === adapter_netplay (接続・パケットルーティング) ===
    adapter_netplay/NetplayManager.cpp
    adapter_netplay/PacketRouter.cpp
    # === game_memory_accessor (MBAAメモリへの直接介入) ===
    game_memory_accessor/GameMonitor.cpp
    game_memory_accessor/StateBuffer.cpp
    game_memory_accessor/MbaaPatcher.cpp
    session_orchestrator/scene/SceneFastBoot.cpp
    # === session_orchestrator (業務とシーンの統括) ===
    session_orchestrator/session/SceneRunner.cpp
    session_orchestrator/session/GameFrameOrchestrator.cpp
    session_orchestrator/scene/SceneCharaSelect.cpp
    session_orchestrator/scene/SceneLoading.cpp
    session_orchestrator/scene/SceneInGame.cpp
    session_orchestrator/scene/SceneRematch.cpp
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
