# ==========================================
# In-Game Hook Library (DLL)
# ==========================================

# We assume minhook and imgui targets are already defined and populated in the root CMakeLists.txt
# so we only declare our local module here.

add_library(cccaster_hook SHARED
    # === common/ — 全層から使う土台 ===
    common/Platform.cpp

    # === mbaa_mem/ — MBAA メモリ操作・検出 ===
    mbaa_mem/dllmain.cpp
    mbaa_mem/MbaaPatcher.cpp
    mbaa_mem/PhaseMonitor.cpp
    mbaa_mem/GameMemory.cpp
    mbaa_mem/MbaaMemTrace.cpp
    mbaa_mem/RealGameMemory.cpp

    # === hook/ — 外部 API フック ===
    hook/DxHook.cpp
    hook/TimeHooks.cpp
    hook/DirectInputHook.cpp
    hook/WndProcHook.cpp

    # === timing/ — 時間管理 ===
    timing/WasapiClock.cpp
    timing/Metronome.cpp

    # === engine/ — メインロジック ===
    engine/SceneRunner.cpp
    engine/GameFrameOrchestrator.cpp
    engine/SceneFastBoot.cpp
    engine/MatchScene.cpp
    engine/SceneInputFilter.cpp

    # === network/ — 通信 ===
    network/UdpSocket.cpp
    network/NetworkSimulator.cpp
    network/NetplayManager.cpp
    network/SyncCodec.cpp
    network/PacketRouter.cpp

    # === sync/ — 同期 ===
    sync/NetplaySession.cpp
    sync/NetplayClock.cpp

    # === rollback/ — ビルド対象から外している ===
    #   入力を 16bit で扱う設計で、現行の GameInput(direction<<16|buttons) と
    #   非互換。繋ぐと方向入力が丸ごと落ちる。ロールバック再開時は
    #   GameInput の上に作り直す。MBAA のステート保存アドレス
    #   (DumpEntryList.hpp) は参照価値があるのでファイルは残す。

    # === ui/ — 表示 ===
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
