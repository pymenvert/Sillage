# Sillage engine — M0 walking skeleton.
# Zero external runtime dependencies: sockets, OSC, WebSocket and the Kalman
# filter are implemented in-tree. vcpkg lands with the first real binary
# dependencies (Eigen, spdlog, uWebSockets, mcap) in M1.

add_library(sillage_core STATIC
    src/calib/collector.cpp
    src/calib/rigid2d.cpp
    src/config/project.cpp
    src/core/background.cpp
    src/core/json.cpp
    src/detect/clustering.cpp
    src/drivers/hokuyo/hokuyo_driver.cpp
    src/drivers/sick/cola.cpp
    src/drivers/sick/sick_driver.cpp
    src/drivers/udpbridge/udp_bridge.cpp
    src/drivers/hokuyo/scip.cpp
    src/eval/metrics.cpp
    src/eval/scenarios.cpp
    src/io/augmenta_osc.cpp
    src/io/ecosystem_outputs.cpp
    src/io/osc.cpp
    src/logic/zones.cpp
    src/net/net.cpp
    src/net/ws_server.cpp
    src/pipeline/pipeline.cpp
    src/record/recorder.cpp
    src/sim/simulator.cpp
    src/track/tracker.cpp
    src/app/engine.cpp
)

target_include_directories(sillage_core PUBLIC src)
target_compile_definitions(sillage_core PUBLIC SILLAGE_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(sillage_core PUBLIC /W4 /WX /permissive- /utf-8)
    target_compile_definitions(sillage_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(sillage_core PUBLIC ws2_32)
else()
    target_compile_options(sillage_core PUBLIC -Wall -Wextra -Werror)
    find_package(Threads REQUIRED)
    target_link_libraries(sillage_core PUBLIC Threads::Threads)
endif()

add_executable(sillage-engine src/main.cpp)
target_link_libraries(sillage-engine PRIVATE sillage_core)

# Make the dev UI page available next to the binary so the embedded HTTP
# server can serve it out of the box.
add_custom_command(TARGET sillage-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/../ui/dev
        $<TARGET_FILE_DIR:sillage-engine>/ui
)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    # MSVC: link gtest against the shared CRT like the rest of the project.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    # Test-only dependency: keep it out of installed/packaged artifacts.
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(sillage_tests
        tests/api_test.cpp
        tests/calib_api_test.cpp
        tests/calib_test.cpp
        tests/clustering_test.cpp
        tests/config_zones_test.cpp
        tests/drivers_test.cpp
        tests/hokuyo_test.cpp
        tests/metrics_test.cpp
        tests/osc_test.cpp
        tests/pipeline_test.cpp
        tests/recorder_test.cpp
        tests/scenarios_test.cpp
        tests/tracker_test.cpp
        tests/ws_handshake_test.cpp
    )
    target_link_libraries(sillage_tests PRIVATE sillage_core GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(sillage_tests DISCOVERY_TIMEOUT 60)
endif()
