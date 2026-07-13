# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

project(exscratcher VERSION 0.1 LANGUAGES CXX)

include(cmake/CPM.cmake)

set(CMAKE_VERBOSE_MAKEFILE on)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unqualified-std-cast-call -pthread")

find_package(Boost 1.83 REQUIRED COMPONENTS context system url)

# Add oneTBB library
CPMAddPackage(
    NAME TBB
    GITHUB_REPOSITORY oneapi-src/oneTBB
    GIT_TAG v2022.3.0
    OPTIONS
        "TBB_TEST OFF"
        "TBB_EXAMPLES OFF"
        "BUILD_SHARED_LIBS ON"
)

# Add SQLiteCpp with bundled SQLite sources
CPMAddPackage(
    NAME SQLiteCpp
    GITHUB_REPOSITORY SRombauts/SQLiteCpp
    GIT_TAG 3.3.3
    OPTIONS
        "SQLITECPP_INTERNAL_SQLITE ON"
        "SQLITE_ENABLE_COLUMN_METADATA ON"
        "SQLITE_ENABLE_JSON1 ON"
        "SQLITE_ENABLE_ASSERT_HANDLER ON"
        "SQLITECPP_BUILD_EXAMPLES OFF"
        "SQLITECPP_BUILD_TESTS OFF"
)

# Add Glaze for fast JSON serialization
CPMAddPackage(
    NAME glaze
    GITHUB_REPOSITORY stephenberry/glaze
    GIT_TAG v5.5.4
    OPTIONS
        "glaze_DEVELOPER_MODE OFF"
)

# Add SDL3 for ImGui windowing with proper Wayland support
#CPMAddPackage(
#    NAME SDL3
#    GITHUB_REPOSITORY libsdl-org/SDL
#    GIT_TAG main
#    OPTIONS
#        "SDL_TEST OFF"
#        "SDL_SHARED ON"
#        "SDL_STATIC OFF"
#        "SDL_X11_XTEST OFF"
#)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO cairo)
if(CAIRO_FOUND)
    CPMAddPackage(
        NAME elements
        GITHUB_REPOSITORY cycfi/elements
        GIT_TAG master
        OPTIONS
            "ELEMENTS_BUILD_EXAMPLES OFF"
    )
    set(ELEMENTS_AVAILABLE ON)
else()
    message(WARNING "cairo not found, Elements UI will not be built. Install libcairo2-dev to enable.")
    set(ELEMENTS_AVAILABLE OFF)
endif()

# Add ThorVG vector graphics library (downloaded via CPM, built with Meson+Ninja)
include(cmake/ThorvgBuild.cmake)

set(core_SOURCES
        src/common/config_helper.hpp
        src/data/bybit/bybit_config.hpp
        src/cockpit/trade_cockpit_config.hpp
        src/data/currency.hpp
        src/common/hex.cpp
        src/common/hex.hpp
        src/common/timedef.hpp
        src/common/data_rectangle.hpp
        src/core/generic_handler.hpp
        src/core/scheduler.cpp
        src/core/scheduler.hpp
        src/data/buoy_candle.cpp
        src/data/buoy_candle.hpp
        src/data/data_controller.hpp
        src/data/entities/common.hpp
        src/data/entities/common.cpp
        src/data/entities/instrument.hpp
        src/data/entities/trade.hpp
        src/data/entities/orderbook_level.hpp
        src/data/bybit/data_manager.cpp
        src/data/bybit/data_manager.hpp
        src/data/orderbook.hpp
        src/data/bybit/entities/orderbookdata.hpp
        src/data/bybit/entities/wallet.hpp
        src/connect/connection_context.hpp
        src/connect/connection_context.cpp
        src/connect/http_query.hpp
        src/connect/http_query.cpp
        src/connect/websocket.hpp
        src/connect/websocket.cpp
        src/datahub/data_model.hpp
        src/datahub/operations.hpp
        src/datahub/query_builder.hpp
        src/datahub/metadata.hpp
        src/datahub/data_sink.hpp
        src/datahub/data_update.hpp
        src/datahub/data_subscription.hpp
        src/datahub/data_feed.hpp
        src/datahub/data_condition.hpp
        src/cockpit/pixel_rect.hpp
        src/cockpit/tvg_ptr.hpp
        src/cockpit/scratcher.hpp
        src/cockpit/scratchers/time_ruler.hpp
        src/cockpit/scratchers/time_ruler.cpp
        src/cockpit/scratchers/price_ruler.hpp
        src/cockpit/scratchers/price_ruler.cpp
        src/cockpit/scratchers/price_indicator.hpp
        src/cockpit/scratchers/price_indicator.cpp
        src/cockpit/scratchers/quote_scratcher.hpp
        src/cockpit/scratchers/quote_scratcher.cpp
        src/cockpit/content_panel.hpp
        src/cockpit/content_panel.cpp
        src/cockpit/instrument_panel.hpp
        src/cockpit/instrument_panel.cpp
        src/cockpit/trade_cockpit.hpp
        src/cockpit/trade_cockpit.cpp
)

add_library(core STATIC ${core_SOURCES})

# Find fontconfig for system font detection
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

# Add xcockpit executable (Elements-based application)
add_executable(xcockpit
            src/main.cpp
            src/app/main_window.cpp
            src/app/main_window.hpp
            src/app/ui_builder.hpp
            src/app/ui_builder.cpp
            src/app/split_direction.hpp
            src/app/instrument_panel_element.hpp
            src/app/instrument_panel_element.cpp
            src/app/tab_bar.hpp
            src/app/tab_bar.cpp
            src/app/panel_node.hpp
            src/app/panel_node.cpp
            src/config.hpp
            src/config.cpp
    )
target_link_libraries(xcockpit PRIVATE elements core glaze::glaze Thorvg::thorvg ${CAIRO_LIBRARIES})
target_include_directories(xcockpit PRIVATE ${CAIRO_INCLUDE_DIRS})

# Copy Elements font resources for xcockpit
set(ELEMENTS_FONT_DIR "${elements_SOURCE_DIR}/resources/fonts")
set(XCOCKPIT_RESOURCES_DIR "$<TARGET_FILE_DIR:xcockpit>/resources")

add_custom_command(
    TARGET xcockpit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XCOCKPIT_RESOURCES_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ELEMENTS_FONT_DIR}/elements_basic.ttf"
        "${ELEMENTS_FONT_DIR}/OpenSans-Regular.ttf"
        "${ELEMENTS_FONT_DIR}/OpenSans-Light.ttf"
        "${ELEMENTS_FONT_DIR}/OpenSans-Bold.ttf"
        "${ELEMENTS_FONT_DIR}/Roboto-Regular.ttf"
        ${XCOCKPIT_RESOURCES_DIR}
    COMMENT "Copying Elements font resources for xcockpit"
)

include_directories(
        contrib
        src
        src/common
        src/connect
        src/core
        src/app
        src/app
        src/cockpit
        src/data
        src/data/entities
        src/datahub
        src/widget
)

target_link_libraries(core PRIVATE ${Boost_LIBRARIES})
target_link_libraries(core PRIVATE OpenSSL::SSL)
target_link_libraries(core PRIVATE TBB::tbb)
target_link_libraries(core PUBLIC SQLiteCpp)
target_link_libraries(core PUBLIC glaze::glaze)
target_link_libraries(core PUBLIC Thorvg::thorvg)

#include(GNUInstallDirs)
#install(TARGETS exscratcher
#    BUNDLE DESTINATION .
#    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
#)

#qt_finalize_executable(exscratcher)

if(NOT ANDROID)

    CPMAddPackage("gh:catchorg/Catch2@3.5.4")

    enable_testing()
    add_custom_target(unit_tests)

    # LIVE tests hit external services: they run unfiltered and carry the `live`
    # CTest label so offline runs exclude them with `ctest -LE live`. Offline
    # executables skip individually live Catch2 cases via the ~[integration] filter.
    # CTest test names are the test source paths: requirement items reference tests
    # by source path, and the JUnit output of `ctest --output-junit` carries the
    # test name, which lets scripts/req_status.py bind results to requirements exactly.
    function(add_unit_test TEST_NAME TEST_FILE)
        cmake_parse_arguments(UNIT "LIVE" "" "" ${ARGN})
        add_executable(${TEST_NAME} ${TEST_FILE})
        target_link_libraries(${TEST_NAME} PRIVATE core)
        target_link_libraries(${TEST_NAME} PRIVATE Catch2::Catch2WithMain)
        target_link_libraries(${TEST_NAME} PRIVATE ${Boost_LIBRARIES})
        target_link_libraries(${TEST_NAME} PRIVATE OpenSSL::SSL)
        target_link_libraries(${TEST_NAME} PRIVATE TBB::tbb)
        target_link_libraries(${TEST_NAME} PRIVATE glaze::glaze)
        target_link_libraries(${TEST_NAME} PRIVATE SQLiteCpp)
        add_dependencies(unit_tests ${TEST_NAME})
        if(UNIT_LIVE)
            add_test(NAME ${TEST_FILE} COMMAND ${TEST_NAME})
            set_tests_properties(${TEST_FILE} PROPERTIES LABELS live)
        else()
            add_test(NAME ${TEST_FILE} COMMAND ${TEST_NAME} "~[integration]")
        endif()
    endfunction()

    # Add each test file as a separate executable
    add_unit_test(test_currency test/data/test_currency.cpp)
    add_unit_test(test_buoy_candle test/core/test_buoy_candle.cpp)
    add_unit_test(test_dao test/datahub/test_dao.cpp)
    add_unit_test(test_http_query test/connect/test_http_query.cpp LIVE)
    add_unit_test(test_websocket test/connect/test_websocket.cpp LIVE)
    add_unit_test(test_data_sink test/datahub/test_data_sink.cpp)
    add_unit_test(test_data_model test/bybit/test_data_model.cpp)
    add_unit_test(test_sorted_data_feed test/datahub/test_sorted_data_feed.cpp)
    add_unit_test(test_sorted_snapshot_data_feed test/datahub/test_sorted_snapshot_data_feed.cpp)
    add_unit_test(test_keyed_snapshot_data_feed test/datahub/test_keyed_snapshot_data_feed.cpp)
    add_unit_test(test_data_manager test/bybit/test_data_manager.cpp LIVE)
    add_unit_test(test_orderbook test/data/test_orderbook.cpp)
    add_unit_test(test_bybit_orderbook test/bybit/test_orderbook.cpp)
    add_unit_test(test_quote_scratcher test/cockpit/scratchers/test_quote_scratcher.cpp)

    # ThorVG render test — links only Catch2 + ThorVG (+ cairo for the bridge test)
    if(THORVG_AVAILABLE AND CAIRO_FOUND)
        add_executable(test_thorvg test/render/test_thorvg.cpp)
        target_link_libraries(test_thorvg PRIVATE Catch2::Catch2WithMain Thorvg::thorvg ${CAIRO_LIBRARIES})
        target_include_directories(test_thorvg PRIVATE ${CAIRO_INCLUDE_DIRS})
        add_dependencies(unit_tests test_thorvg)
        add_test(NAME test/render/test_thorvg.cpp COMMAND test_thorvg)

        # TimeRuler image-series test — emits PNGs of the rendered time axis at multiple zoom
        # levels into ${CMAKE_BINARY_DIR}/test_output/time_ruler/ for visual analysis.
        add_executable(test_time_ruler test/render/test_time_ruler.cpp)
        target_link_libraries(test_time_ruler PRIVATE core Catch2::Catch2WithMain ${CAIRO_LIBRARIES})
        target_include_directories(test_time_ruler PRIVATE ${CAIRO_INCLUDE_DIRS})
        target_compile_definitions(test_time_ruler PRIVATE
            "TIME_RULER_OUTPUT_DIR=\"${CMAKE_BINARY_DIR}/test_output/time_ruler\""
            "ELSCRATCHER_RUNTIME_DIR=\"$<TARGET_FILE_DIR:xcockpit>\""
        )
        add_dependencies(test_time_ruler xcockpit)  # ensures resources/ is populated
    endif()

endif()

