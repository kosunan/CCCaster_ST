cmake_minimum_required(VERSION 3.14)
project(DummyPeer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ================================================================
# DummyPeer v2 — WIN32 GUIアプリ (WASAPI安定動作のため)
# フェーズA: main→WinMain 変更に伴い WIN32 フラグを追加
# ================================================================
add_executable(DummyPeer WIN32
    src/main.cpp
    src/DummyPeer.cpp
    src/TestRunner.cpp
    src/NegotiationTest.cpp
    src/SyncTest.cpp
    src/FullTest.cpp
    src/GameTest.cpp
    src/NetworkSimulator.cpp
    src/InputGenerator.cpp
    src/SyncResponder.cpp
    src/NegotiationResponder.cpp
    src/PhaseHandler.cpp
    src/ReportWriter.cpp
    # フェーズC: 疑似画面遷移エンジン
    src/SceneStateMachine.cpp
    src/DummySceneCharaSelect.cpp
    src/DummySceneLoading.cpp
    src/DummySceneInGame.cpp
    src/DummySceneRematch.cpp
    # フェーズD: コントローラ入力シミュレーション
    src/ControllerInputSim.cpp
    # フェーズE: E2Eテスト
    src/E2ETest.cpp
)

target_include_directories(DummyPeer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # 本体側の公開APIヘッダー（パケット定義の共有用）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

# フェーズA: avrt追加（WASAPI優先スレッド制御用）
target_link_libraries(DummyPeer PRIVATE ws2_32 winmm ole32 uuid avrt)
