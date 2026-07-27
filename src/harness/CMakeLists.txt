# ==========================================
# harness — ゲーム無しで同期ロジックを走らせる実行ファイル
# ==========================================
# core_dll の同期・通信層をそのままリンクし、ゲームメモリだけ FakeGame に
# 差し替える。ImGui / DirectInput / MinHook を引き込む層は harness_stubs.cpp
# で置き換えるため、ここには含めない。
#
# 実行例:
#   harness.exe --host --port 7600 --out host.txt
#   harness.exe --ip 127.0.0.1 --port 7600 --local-port 7601 --out client.txt

set(CORE ${CMAKE_SOURCE_DIR}/src/core_dll)

add_executable(harness
    harness_main.cpp
    FakeGame.cpp
    harness_stubs.cpp

    # ── 検証対象（実コード）──
    ${CORE}/engine/SceneRunner.cpp
    ${CORE}/engine/MatchScene.cpp
    ${CORE}/engine/SceneInputFilter.cpp
    # SceneFastBoot.cpp は harness_stubs.cpp で置換（seam 外の実アドレスを触るため）
    ${CORE}/sync/NetplaySession.cpp
    ${CORE}/sync/NetplayClock.cpp
    ${CORE}/network/SyncCodec.cpp
    ${CORE}/network/NetplayManager.cpp
    ${CORE}/network/PacketRouter.cpp
    ${CORE}/network/UdpSocket.cpp
    ${CORE}/network/NetworkSimulator.cpp
    ${CORE}/timing/Metronome.cpp
    ${CORE}/timing/WasapiClock.cpp
    ${CORE}/mbaa_mem/PhaseMonitor.cpp
    ${CORE}/mbaa_mem/GameMemory.cpp
)

target_include_directories(harness PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${asio_SOURCE_DIR}/asio/include   # UdpSocket が asio を使う
)

# ws2_32/mswsock: UDP / ole32: WASAPI クロック（オーディオデバイスが無ければ QPC に落ちる）
target_link_libraries(harness PRIVATE ws2_32 mswsock ole32 winmm)

if(WIN32)
    target_link_options(harness PRIVATE -static -static-libgcc -static-libstdc++)
endif()
